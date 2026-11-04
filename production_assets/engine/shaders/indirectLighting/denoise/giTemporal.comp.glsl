#version 450
// Shared full resolution temporal denoiser (M4-G1). Signal contract: rgb = radiance-like value, a = confidence in [0,1]
// (Indirect: cosine-weighted radiance = irradiance / pi, a = confidence). Settings come from a pipeline "TemporalDenoisers"
// entry (BufferLinks { "Denoise": <Name> }), so GI and, later, reflections share the shader with their own parameters and
// per-effect history validity.
//   reprojection: Velocity (+ jitter delta, the G-buffer grid is jittered), bilinear 2x2 history taps;
//   disocclusion: every tap must match the expected linear depth (DepthTolerance, DepthBias) and the normal (NormalThreshold);
//                 no tap -> history length 1 (the disocclusion mask is Meta.x == 1);
//   variance clamp: history limited to mean +- ClampSigma * sigma of the 3x3 current taps ClampStride texels apart
//                 (screen probes: one tile, the upsampled signal is smooth inside a tile);
//   alpha = 1 / history length (MaxHistory cap): fast while the history is short;
//   confidence = accumulated validity x min(1, history length / ConfidenceHistory);
//   NaN/Inf guard before the history write.
// Output: rgba16f (rgb, confidence). Meta: rg16f, x = history length, y = accumulated validity. Sky texels: 0.
#include "temporalCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection;
    mat4 PreviousViewProjection;
    mat4 PreviousModel;
    vec4 CameraParams;
    vec4 FrameState;
} engine_Frame;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Velocity;
layout(set=0,binding=11) uniform sampler2D Current;
layout(set=0,binding=12) uniform sampler2D Previous;
layout(set=0,binding=13) uniform sampler2D PreviousMeta;
layout(set=0,binding=14) uniform sampler2D PreviousDepth;
layout(set=0,binding=15) uniform sampler2D PreviousNormals;
layout(set=0,binding=16,rgba16f) uniform writeonly image2D Output;
layout(set=0,binding=17,rg16f) uniform writeonly image2D Meta;

const float TD_SKY_DEPTH=0.999999;

void main() {
    ivec2 pixelOut=ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize=imageSize(Output);
    if(any(greaterThanEqual(pixelOut,outSize)))return;
    vec2 uv=(vec2(pixelOut)+0.5)/vec2(outSize);
    ivec2 depthSize=textureSize(Depth,0);
    ivec2 pixel=min(ivec2(uv*vec2(depthSize)),depthSize-1);
    float depth=texelFetch(Depth,pixel,0).r;
    if(depth>=TD_SKY_DEPTH) {
        imageStore(Output,pixelOut,vec4(0.0));
        imageStore(Meta,pixelOut,vec4(0.0));
        return;
    }
    vec4 current=tdSanitize(textureLod(Current,uv,0.0),TD_HALF_MAX);
    float historyLength=1.0;
    vec3 color=current.rgb;
    float validity=current.a;
    if(tdHistoryValid()) {
        vec3 normal=normalize(texelFetch(Normals,pixel,0).xyz);
        vec3 position=tdWorldPosition((vec2(pixel)+0.5)/vec2(depthSize),depth,engine_Frame.InvViewProjection,engine_Frame.CameraParams);
        float expected=(engine_Frame.PreviousViewProjection*vec4(position,1.0)).w;
        vec2 previousUv=uv-texelFetch(Velocity,pixel,0).xy-engine_Frame.FrameState.zw;
        ivec2 previousSize=textureSize(Previous,0);
        vec2 footprint=previousUv*vec2(previousSize)-0.5;
        ivec2 base=ivec2(floor(footprint));
        vec2 fraction=footprint-vec2(base);
        vec4 history=vec4(0.0);
        vec2 meta=vec2(0.0);
        float total=0.0;
        for(int y=0;y<2;++y) {
            for(int x=0;x<2;++x) {
                ivec2 tap=base+ivec2(x,y);
                if(any(lessThan(tap,ivec2(0)))||any(greaterThanEqual(tap,previousSize)))continue;
                ivec2 depthTap=min(tap,textureSize(PreviousDepth,0)-1);
                ivec2 normalTap=min(tap,textureSize(PreviousNormals,0)-1);
                float previousDepth=tdLinearDepth(texelFetch(PreviousDepth,depthTap,0).r,engine_Frame.CameraParams);
                vec3 previousNormal=normalize(texelFetch(PreviousNormals,normalTap,0).xyz);
                if(!tdSameSurface(expected,previousDepth,normal,previousNormal))continue;
                float weight=(x==0?1.0-fraction.x:fraction.x)*(y==0?1.0-fraction.y:fraction.y);
                history+=texelFetch(Previous,tap,0)*weight;
                meta+=texelFetch(PreviousMeta,min(tap,textureSize(PreviousMeta,0)-1),0).xy*weight;
                total+=weight;
            }
        }
        if(total>1e-3) {
            history/=total;
            meta/=total;
            vec3 mean=vec3(0.0);
            vec3 squared=vec3(0.0);
            float count=0.0;
            ivec2 currentSize=textureSize(Current,0);
            ivec2 center=min(ivec2(uv*vec2(currentSize)),currentSize-1);
            int stride=max(Denoise.History.z,1);
            for(int y=-1;y<=1;++y) {
                for(int x=-1;x<=1;++x) {
                    vec4 value=texelFetch(Current,clamp(center+ivec2(x,y)*stride,ivec2(0),currentSize-1),0);
                    if(value.a<=0.0&&value.r+value.g+value.b<=0.0)continue;
                    mean+=value.rgb;
                    squared+=value.rgb*value.rgb;
                    count+=1.0;
                }
            }
            if(count>0.0) {
                mean/=count;
                vec3 sigma=sqrt(max(squared/count-mean*mean,vec3(0.0)));
                history.rgb=tdClampToBox(history.rgb,mean,sigma,Denoise.Clamp.x);
            }
            historyLength=min(meta.x+1.0,tdMaxHistory());
            float alpha=tdHistoryAlpha(historyLength);
            color=mix(history.rgb,current.rgb,alpha);
            validity=mix(meta.y,current.a,alpha);
        }
    }
    float confidenceHistory=max(Denoise.Rejection.z,1.0);
    float confidence=clamp(validity,0.0,1.0)*clamp(historyLength/confidenceHistory,0.0,1.0);
    imageStore(Output,pixelOut,tdSanitize(vec4(color,confidence),TD_HALF_MAX));
    imageStore(Meta,pixelOut,tdSanitize(vec4(historyLength,validity,0.0,0.0),TD_HALF_MAX));
}
