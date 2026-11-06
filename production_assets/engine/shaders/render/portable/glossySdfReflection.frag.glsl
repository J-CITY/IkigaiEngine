#version 450

#include "../../indirectLighting/screenProbes/screenProbeSettings.glsl"
#include "../../indirectLighting/screenProbes/screenProbeMath.glsl"
#include "../../raytracing/glossyReflectionRoughness.glsl"

layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Input;
layout(set=0,binding=9) uniform sampler2D Weight;
layout(set=0,binding=10) uniform sampler2D Depth;
layout(set=0,binding=11) uniform sampler2D Normals;
layout(set=0,binding=12) uniform sampler2D Albedo;
layout(set=0,binding=13) uniform sampler2D Roughness;
layout(set=0,binding=14) uniform sampler2D EnvironmentSpecular;
layout(set=0,binding=15) uniform samplerCube Environment;
layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4 View;
    mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection;
    mat4 PreviousViewProjection;
    mat4 PreviousModel;
    vec4 CameraParams;
    vec4 FrameState;
} engine_Frame;

#include "../../indirectLighting/screenProbes/spSdfMarch.glsl"

uint rngState;
float randomFloat() {
    return spHashFloat(rngState++);
}

#include "../../raytracing/reflectionGgx.glsl"

const int GlossySdfSamples=4;
const float SsrSkipConfidence=0.9;

vec3 worldPosition(vec2 uv,float depth) {
    uv.y=mix(uv.y,1.0-uv.y,engine_Frame.CameraParams.w);
    vec4 position=engine_Frame.InvViewProjection*vec4(uv*2.0-1.0,mix(depth*2.0-1.0,depth,engine_Frame.CameraParams.z),1.0);
    return position.xyz/position.w;
}

vec3 traceGlossySdf(vec3 position,vec3 normal,vec3 view,float roughness,vec3 f0,vec3 ibl) {
    vec3 reflected=vec3(0.0);
    int hitCount=0;
    for(int sampleIndex=0;sampleIndex<GlossySdfSamples;++sampleIndex) {
        vec3 direction=reflectedDirection(normal,view,roughness);
        float normalLight=dot(normal,direction);
        if(normalLight<=0.0)continue;
        vec3 radiance;
        if(!spTraceSdf(position,normal,direction,radiance))continue;
        reflected+=weightReflectionRadiance(radiance,normal,view,direction,normalLight,roughness,f0);
        ++hitCount;
    }
    // IBL already includes the receiver BRDF and fills the share without a geometry hit.
    return (reflected+float(GlossySdfSamples-hitCount)*ibl)/float(GlossySdfSamples);
}

void main() {
    vec4 ssr=texture(Input,UV);
    vec3 screenReflection=ssr.a>1e-5?ssr.rgb*texture(Weight,UV).rgb/ssr.a:vec3(0.0);
    if(!spChainEnabled(SP_CHAIN_SDF_ACTIVE)) {
        // A different scene still uses the original SSR confidence contract.
        Color=vec4(screenReflection,ssr.a);
        return;
    }

    float depth=textureLod(Depth,UV,0.0).r;
    if(depth>=SP_SKY_DEPTH) {
        Color=vec4(0.0);
        return;
    }
    float roughness=clamp(textureLod(Roughness,UV,0.0).r,0.04,1.0);
    vec3 ibl=textureLod(EnvironmentSpecular,UV,0.0).rgb;
    float glossyWeight=glossyTraceWeight(roughness);
    if(glossyWeight==0.0) {
        Color=vec4(ibl,1.0);
        return;
    }

    float confidence=clamp(ssr.a,0.0,1.0);
    vec3 fallback=ibl;
    if(confidence<SsrSkipConfidence) {
        vec3 position=worldPosition(UV,depth);
        vec3 normal=normalize(textureLod(Normals,UV,0.0).xyz);
        vec3 view=normalize(engine_UBO.ViewPos-position);
        vec4 material=textureLod(Albedo,UV,0.0);
        vec3 f0=mix(vec3(0.04),material.rgb,material.a);
        uvec2 size=uvec2(textureSize(Input,0));
        uvec2 pixel=uvec2(UV*vec2(size));
        rngState=spHash(pixel.x+pixel.y*size.x+uint(engine_UBO.FrameCount)*9781u);
        float rayWeight=1.0-smoothstep(0.8,SsrSkipConfidence,confidence);
        fallback=mix(ibl,traceGlossySdf(position,normal,view,roughness,f0,ibl),rayWeight);
    }
    vec3 reflected=mix(ibl,mix(fallback,screenReflection,confidence),glossyWeight);
    Color=vec4(max(reflected,vec3(0.0)),1.0);
}
