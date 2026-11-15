#version 450
// Probe level temporal filter (M4-G1, ScreenProbes.UseTemporalV2): accumulates the integrated probe irradiance over frames
// before the spatial filter (SVGF order: temporal accumulation, then spatial), so the spatial and full resolution passes see
// a signal whose Monte Carlo noise is already averaged. Per probe history (probe grid, one texel per probe):
//   Output   rgba16f  rgb = Intensity * E / pi (accumulated), a = accumulated confidence (contract of SPIntegrate)
//   Meta     rgba16f  x = history length (frames), y = luminance variance (exponentially weighted), z = fast luminance
//   Geometry rgba32f  xyz = world position of the probe, w = 1 for a probe with history
// Reprojection: the probe pixel goes to the previous frame with Velocity (+ jitter delta, as SPTemporal); the previous depth
// and normal at that pixel reject disocclusion; the 2x2 previous probes around it are weighted bilinearly and each must lie
// on the probe plane (DepthTolerance) with a matching normal. Variance clamp: history limited to mean +- ClampSigma * sigma of
// the valid current probes of the 3x3 neighbourhood on the same surface. alpha = 1 / history length (MaxHistory cap);
// anti-lag shortens the history when the fast average leaves the noise band. History resets when Denoise.History.y is 0.
#include "screenProbeCommon.glsl"
#include "../denoise/temporalCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Velocity;
layout(set=0,binding=11) uniform sampler2D ProbeInfo;
layout(set=0,binding=12) uniform sampler2D Current;
layout(set=0,binding=13) uniform sampler2D PreviousOutput;
layout(set=0,binding=14) uniform sampler2D PreviousMeta;
layout(set=0,binding=15) uniform sampler2D PreviousGeometry;
layout(set=0,binding=16) uniform sampler2D PreviousDepth;
layout(set=0,binding=17) uniform sampler2D PreviousNormals;
layout(set=0,binding=18,rgba16f) uniform writeonly image2D Output;
layout(set=0,binding=19,rgba16f) uniform writeonly image2D Meta;
layout(set=0,binding=20,rgba32f) uniform writeonly image2D Geometry;

bool probeSurface(ivec2 probe,vec2 size,out vec3 position,out vec3 normal) {
    ivec2 pixel;
    position=vec3(0.0);
    normal=vec3(0.0,0.0,1.0);
    if(!spProbePixel(ProbeInfo,probe,pixel))return false;
    position=spWorldPosition((vec2(pixel)+0.5)/size,texelFetch(Depth,pixel,0).r);
    normal=normalize(texelFetch(Normals,pixel,0).xyz);
    return true;
}

bool validSample(vec4 value) { return value.a>0.0||value.r+value.g+value.b>0.0; }

void store(ivec2 probe,vec4 value,vec4 meta,vec4 geometry) {
    imageStore(Output,probe,tdSanitize(value,TD_HALF_MAX));
    imageStore(Meta,probe,tdSanitize(meta,TD_HALF_MAX));
    vec4 safeGeometry=geometry;
    if(!tdFinite(geometry.x)||!tdFinite(geometry.y)||!tdFinite(geometry.z))safeGeometry=vec4(0.0);
    imageStore(Geometry,probe,safeGeometry);
}

// Mean and standard deviation of the valid current probes in the 3x3 neighbourhood that lie on the centre probe's surface.
bool neighbourhood(ivec2 probe,ivec2 grid,vec2 size,vec3 position,vec3 normal,float tolerance,out vec3 mean,out vec3 sigma) {
    mean=vec3(0.0);
    sigma=vec3(0.0);
    vec3 squared=vec3(0.0);
    float count=0.0;
    for(int y=-1;y<=1;++y) {
        for(int x=-1;x<=1;++x) {
            ivec2 neighbour=probe+ivec2(x,y);
            if(any(lessThan(neighbour,ivec2(0)))||any(greaterThanEqual(neighbour,grid)))continue;
            vec3 p,n;
            if(!probeSurface(neighbour,size,p,n))continue;
            if(abs(dot(normal,p-position))>tolerance||dot(normal,n)<Denoise.Rejection.y)continue;
            vec4 value=texelFetch(Current,neighbour,0);
            if(!validSample(value))continue;
            mean+=value.rgb;
            squared+=value.rgb*value.rgb;
            count+=1.0;
        }
    }
    if(count<2.0)return false;
    mean/=count;
    sigma=sqrt(max(squared/count-mean*mean,vec3(0.0)));
    return true;
}

void main() {
    ivec2 probe=ivec2(gl_GlobalInvocationID.xy);
    ivec2 grid=imageSize(Output);
    if(any(greaterThanEqual(probe,grid)))return;
    vec4 current=texelFetch(Current,probe,0);
    vec2 size=vec2(textureSize(Depth,0));
    vec3 position,normal;
    if(!probeSurface(probe,size,position,normal)) {
        store(probe,vec4(0.0),vec4(0.0),vec4(0.0));
        return;
    }
    // SPIntegrate writes 0 for probes with an invalid sample: keep them out of the history.
    if(!validSample(current)) {
        store(probe,vec4(0.0),vec4(0.0),vec4(position,0.0));
        return;
    }
    float currentLuma=tdLuminance(current.rgb);
    vec4 result=current;
    float historyLength=1.0;
    float variance=0.0;
    float fastLuma=currentLuma;
    if(tdHistoryValid()) {
        ivec2 pixel;
        spProbePixel(ProbeInfo,probe,pixel);
        vec2 uv=(vec2(pixel)+0.5)/size;
        vec2 previousUv=uv-texelFetch(Velocity,pixel,0).xy-engine_Frame.FrameState.zw;
        ivec2 previousSize=textureSize(PreviousDepth,0);
        float tolerance=tdDepthTolerance(spViewDepth(position));
        if(all(greaterThanEqual(previousUv,vec2(0.0)))&&all(lessThan(previousUv,vec2(1.0)))) {
            ivec2 previousPixel=min(ivec2(previousUv*vec2(previousSize)),previousSize-1);
            float expected=(engine_Frame.PreviousViewProjection*vec4(position,1.0)).w;
            float previousDepth=spLinearDepth(texelFetch(PreviousDepth,previousPixel,0).r);
            vec3 previousNormal=normalize(texelFetch(PreviousNormals,min(previousPixel,textureSize(PreviousNormals,0)-1),0).xyz);
            if(tdSameSurface(expected,previousDepth,normal,previousNormal)) {
                ivec2 previousGrid=textureSize(PreviousOutput,0);
                vec2 g=previousUv*vec2(previousSize)/float(spTileSize())-0.5;
                ivec2 base=ivec2(floor(g));
                vec2 f=g-vec2(base);
                vec4 history=vec4(0.0);
                vec4 meta=vec4(0.0);
                float total=0.0;
                for(int y=0;y<2;++y) {
                    for(int x=0;x<2;++x) {
                        ivec2 tap=base+ivec2(x,y);
                        if(any(lessThan(tap,ivec2(0)))||any(greaterThanEqual(tap,previousGrid)))continue;
                        vec4 geometry=texelFetch(PreviousGeometry,tap,0);
                        if(geometry.w<0.5||abs(dot(normal,geometry.xyz-position))>tolerance)continue;
                        // Normal of the previous probe, read at its pixel of the previous frame.
                        vec3 screen=spClipToScreen(engine_Frame.PreviousViewProjection*vec4(geometry.xyz,1.0),vec2(previousSize));
                        ivec2 tapPixel=clamp(ivec2(screen.xy),ivec2(0),previousSize-1);
                        if(dot(normal,normalize(texelFetch(PreviousNormals,tapPixel,0).xyz))<Denoise.Rejection.y)continue;
                        float weight=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y)+1e-3;
                        history+=texelFetch(PreviousOutput,tap,0)*weight;
                        meta+=texelFetch(PreviousMeta,tap,0)*weight;
                        total+=weight;
                    }
                }
                if(total>0.0) {
                    history/=total;
                    meta/=total;
                    vec3 mean,sigma;
                    if(neighbourhood(probe,grid,size,position,normal,tolerance,mean,sigma)) {
                        history.rgb=tdClampToBox(history.rgb,mean,sigma,Denoise.Clamp.x);
                    }
                    float historyLuma=tdLuminance(history.rgb);
                    historyLength=min(meta.x+1.0,tdMaxHistory());
                    float fastAlpha=max(tdHistoryAlpha(historyLength),1.0/max(Denoise.Clamp.z,1.0));
                    fastLuma=mix(meta.z,currentLuma,fastAlpha);
                    historyLength=tdAntiLag(historyLength,meta.y,historyLuma,fastLuma);
                    float alpha=tdHistoryAlpha(historyLength);
                    variance=tdVarianceUpdate(meta.y,historyLuma,currentLuma,alpha);
                    result=mix(history,current,alpha);
                }
            }
        }
    }
    store(probe,result,vec4(historyLength,variance,fastLuma,0.0),vec4(position,1.0));
}
