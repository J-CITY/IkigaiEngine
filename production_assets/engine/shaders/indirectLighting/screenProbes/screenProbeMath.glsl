#ifndef IKIGAI_SCREEN_PROBE_MATH_GLSL
#define IKIGAI_SCREEN_PROBE_MATH_GLSL
// Screen probe gather: constants and pure helpers shared by the material compute stages (screenProbeCommon.glsl) and the
// ray query stage (spRayQueryTrace.comp). Needs the ScreenProbe settings block (screenProbeSettings.glsl) declared first.

const int SP_DEBUG_OFF=0;
const int SP_DEBUG_PROBES=1;
const int SP_DEBUG_HIT_MISS=2;
const int SP_DEBUG_CONFIDENCE=3;
const int SP_DEBUG_RAW=4;
const int SP_DEBUG_FILTERED=5;
const int SP_DEBUG_SOURCE=6;
const float SP_SKY_DEPTH=0.999999;

// Trace chain links (ScreenProbe.Chain.x).
const int SP_CHAIN_SCREEN=1;
const int SP_CHAIN_RAY_QUERY=2;
const int SP_CHAIN_DDGI=4;
const int SP_CHAIN_ENVIRONMENT=8;
const int SP_CHAIN_SDF_ACTIVE=16;

// Radiance atlas alpha: kind of the sample (exact in half float). Kinds *_AFTER_RAY mark rays that were traced with a ray
// query and fell back further, so the number of ray query rays per frame can be read back from the atlas.
const float SP_KIND_HIT=1.0;
const float SP_KIND_RAY_QUERY=0.875;
const float SP_KIND_SURFACE_CACHE=0.8125;
const float SP_KIND_SDF_CACHE=0.765625;
const float SP_KIND_SDF=0.75;
const float SP_KIND_DDGI_AFTER_RAY=0.6875;
const float SP_KIND_DDGI=0.625;
const float SP_KIND_MISS_AFTER_RAY=0.5625;
const float SP_KIND_MISS=0.5;

// Source of an atlas sample.
const int SP_SOURCE_INVALID=0;
const int SP_SOURCE_SCREEN=1;
const int SP_SOURCE_RAY_QUERY=2;
const int SP_SOURCE_DDGI=3;
const int SP_SOURCE_ENVIRONMENT=4;
const int SP_SOURCE_SDF=5;

int spTileSize() { return max(ScreenProbe.Layout.x,1); }
int spDirectionSide() { return clamp(ScreenProbe.Layout.y,1,16); }
int spDebugMode() { return ScreenProbe.Layout.w; }
bool spChainEnabled(int link) { return (ScreenProbe.Chain.x&link)!=0; }

int spSampleSource(float kind) {
    if(kind>0.9375)return SP_SOURCE_SCREEN;
    if(kind>0.78)return SP_SOURCE_RAY_QUERY;
    if(kind==SP_KIND_SDF||kind==SP_KIND_SDF_CACHE)return SP_SOURCE_SDF;
    if(kind>0.59)return SP_SOURCE_DDGI;
    if(kind>0.25)return SP_SOURCE_ENVIRONMENT;
    return SP_SOURCE_INVALID;
}
// Confidence of one atlas sample: screen hits 1 > traced hits RayQueryConfidence > DDGI / environment MissConfidence.
float spSampleConfidence(float kind) {
    int source=spSampleSource(kind);
    if(source==SP_SOURCE_SCREEN)return 1.0;
    if(source==SP_SOURCE_RAY_QUERY||source==SP_SOURCE_SDF)return clamp(ScreenProbe.RayQuery.y,0.0,1.0);
    if(source==SP_SOURCE_INVALID)return 0.0;
    return clamp(ScreenProbe.Trace.z,0.0,1.0);
}

uint spHash(uint x) {
    x^=x>>16;
    x*=0x7feb352du;
    x^=x>>15;
    x*=0x846ca68bu;
    x^=x>>16;
    return x;
}
float spHashFloat(uint x) { return float(spHash(x)>>8)*(1.0/16777216.0); }
// fract(n * alpha) evaluated in 32-bit fixed point so it stays exact for any frame index.
float spFixedFract(uint n,uint alpha) { return float((n*alpha)>>8)*(1.0/16777216.0); }
// R2 low-discrepancy sequence (plastic constant), offset by 0.5.
vec2 spR2(uint n) { return fract(vec2(0.5)+vec2(spFixedFract(n,3242174889u),spFixedFract(n,2447445413u))); }

// Shirley-Chiu concentric square -> disk, then Malley's lift: cosine-weighted hemisphere (z up).
// Equal-area strata of the square are equal-weight strata of the cosine lobe.
vec3 spCosineHemisphere(vec2 u) {
    vec2 a=u*2.0-1.0;
    vec2 d=vec2(0.0);
    if(a.x!=0.0||a.y!=0.0) {
        float r,phi;
        if(abs(a.x)>abs(a.y)) {
            r=a.x;
            phi=0.78539816*(a.y/a.x);
        }
        else {
            r=a.y;
            phi=1.57079633-0.78539816*(a.x/a.y);
        }
        d=r*vec2(cos(phi),sin(phi));
    }
    return vec3(d,sqrt(max(0.0,1.0-dot(d,d))));
}
// Branchless orthonormal basis (Duff et al. 2017).
void spBasis(vec3 n,out vec3 t,out vec3 b) {
    float s=n.z>=0.0?1.0:-1.0;
    float a=-1.0/(s+n.z);
    float c=n.x*n.y*a;
    t=vec3(1.0+s*n.x*n.x*a,s*c,-s*n.x);
    b=vec3(c,s+n.y*n.y*a,-n.y);
}

// World direction of atlas cell `cell` of probe `probe`: stratified cosine-weighted about the probe normal, R2 jitter inside
// the cell and a per-frame rotation, both decorrelated per probe. Every trace stage derives the same direction.
vec3 spProbeDirection(ivec2 probe,ivec2 cell,vec3 normal,uint frame) {
    int side=spDirectionSide();
    uint seed=spHash(uint(probe.x)+uint(probe.y)*65536u);
    vec2 jitter=fract(spR2(frame)+vec2(spHashFloat(seed),spHashFloat(seed^0x9e3779b9u)));
    vec3 local=spCosineHemisphere((vec2(cell)+jitter)/float(side));
    float angle=6.2831853*fract(spFixedFract(frame,2654435769u)+spHashFloat(seed*3u+1u));
    float c=cos(angle),sn=sin(angle);
    local.xy=vec2(c*local.x-sn*local.y,sn*local.x+c*local.y);
    vec3 tangent,bitangent;
    spBasis(normal,tangent,bitangent);
    return normalize(tangent*local.x+bitangent*local.y+normal*local.z);
}

// Probe of tile `probe`: texel position, or false for sky / empty tiles. ProbeInfo stores the offset inside the tile.
bool spProbePixel(sampler2D probeInfo,ivec2 probe,out ivec2 pixel) {
    vec4 info=texelFetch(probeInfo,probe,0);
    pixel=probe*spTileSize()+ivec2(info.xy+0.5);
    return info.z>0.5;
}
#endif
