#version 450
// Temporal filter of the upsampled screen probe irradiance (own per-stage history, independent of the RT history):
// reprojection with Velocity (+ jitter delta, as rayDenoise), per-tap disocclusion test (linear depth against the
// reprojected depth, normal), variance clamp of the history to the 3x3 neighbourhood of the current frame,
// history length (MaxHistory) and blend 1/length. History resets when engine_Frame.FrameState.x is 0 (camera cut,
// resize, pipeline reload) or when UseHistory is 0.
// Output: rgb = irradiance / pi, a = confidence = filtered probe validity x min(1, length / ConfidenceHistory).
// Meta: x = history length, y = filtered probe validity. Debug modes PROBES / HIT_MISS / CONFIDENCE / SOURCE write a
// visualisation with a = 1; RAW and FILTERED skip the temporal filter.
#include "screenProbeCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Velocity;
layout(set=0,binding=11) uniform sampler2D Current;
layout(set=0,binding=12) uniform sampler2D Previous;
layout(set=0,binding=13) uniform sampler2D PreviousMeta;
layout(set=0,binding=14) uniform sampler2D PreviousDepth;
layout(set=0,binding=15) uniform sampler2D PreviousNormals;
layout(set=0,binding=16) uniform sampler2D ProbeInfo;
layout(set=0,binding=17) uniform sampler2D Radiance;
layout(set=0,binding=18,rgba16f) uniform writeonly image2D Output;
layout(set=0,binding=19,rg16f) uniform writeonly image2D Meta;

vec3 debugColor(int mode,ivec2 pixel,vec4 current) {
    if(mode==SP_DEBUG_CONFIDENCE)return vec3(clamp(current.a,0.0,1.0));
    ivec2 probe=pixel/spTileSize();
    ivec2 probePixel;
    if(any(greaterThanEqual(probe,textureSize(ProbeInfo,0)))||!spProbePixel(ProbeInfo,probe,probePixel))return vec3(0.3,0.0,0.0);
    if(mode==SP_DEBUG_PROBES) {
        ivec2 offset=abs(pixel-probePixel);
        return max(offset.x,offset.y)<=1?vec3(0.0,1.0,0.0):vec3(0.02);
    }
    int side=spDirectionSide();
    // Shares of the ray sources in this probe: screen, ray query, DDGI, environment.
    vec4 shares=vec4(0.0);
    for(int y=0;y<side;++y) {
        for(int x=0;x<side;++x) {
            int source=spSampleSource(texelFetch(Radiance,probe*side+ivec2(x,y),0).a);
            shares+=vec4(source==SP_SOURCE_SCREEN?1.0:0.0,source==SP_SOURCE_RAY_QUERY?1.0:0.0,source==SP_SOURCE_DDGI?1.0:0.0,
                source==SP_SOURCE_ENVIRONMENT?1.0:0.0);
        }
    }
    shares/=float(side*side);
    if(mode==SP_DEBUG_SOURCE) {
        // Screen green, ray query blue, DDGI magenta, environment red: G = screen, B = ray query + DDGI, R = DDGI + environment.
        return vec3(shares.z+shares.w,shares.x,shares.y+shares.z);
    }
    // HIT_MISS: screen hits green, every other source red.
    return vec3(shares.y+shares.z+shares.w,shares.x,0.0);
}

void main() {
    ivec2 pixelOut=ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize=imageSize(Output);
    if(any(greaterThanEqual(pixelOut,outSize)))return;
    vec2 uv=(vec2(pixelOut)+0.5)/vec2(outSize);
    ivec2 depthSize=textureSize(Depth,0);
    ivec2 pixel=min(ivec2(uv*vec2(depthSize)),depthSize-1);
    vec4 current=textureLod(Current,uv,0.0);
    int mode=spDebugMode();
    if(mode==SP_DEBUG_PROBES||mode==SP_DEBUG_HIT_MISS||mode==SP_DEBUG_CONFIDENCE||mode==SP_DEBUG_SOURCE) {
        imageStore(Output,pixelOut,vec4(debugColor(mode,pixel,current),1.0));
        imageStore(Meta,pixelOut,vec4(1.0,current.a,0.0,0.0));
        return;
    }
    float depth=texelFetch(Depth,pixel,0).r;
    if(depth>=SP_SKY_DEPTH) {
        imageStore(Output,pixelOut,vec4(0.0));
        imageStore(Meta,pixelOut,vec4(0.0));
        return;
    }
    bool historyEnabled=ScreenProbe.Filter.x!=0&&mode!=SP_DEBUG_RAW&&mode!=SP_DEBUG_FILTERED;
    float confidenceHistory=max(ScreenProbe.Weights.w,1.0);
    if(!historyEnabled) {
        imageStore(Output,pixelOut,current);
        imageStore(Meta,pixelOut,vec4(1.0,current.a,0.0,0.0));
        return;
    }
    float historyLength=1.0;
    vec3 color=current.rgb;
    float validity=current.a;
    if(engine_Frame.FrameState.x>0.5) {
        vec3 normal=normalize(texelFetch(Normals,pixel,0).xyz);
        vec3 position=spWorldPosition((vec2(pixel)+0.5)/vec2(depthSize),depth);
        float expected=(engine_Frame.PreviousViewProjection*vec4(position,1.0)).w;
        float tolerance=0.03*expected+0.01;
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
                if(abs(spLinearDepth(texelFetch(PreviousDepth,depthTap,0).r)-expected)>tolerance)continue;
                ivec2 normalTap=min(tap,textureSize(PreviousNormals,0)-1);
                if(dot(normal,normalize(texelFetch(PreviousNormals,normalTap,0).xyz))<0.9)continue;
                float weight=(x==0?1.0-fraction.x:fraction.x)*(y==0?1.0-fraction.y:fraction.y);
                history+=texelFetch(Previous,tap,0)*weight;
                meta+=texelFetch(PreviousMeta,min(tap,textureSize(PreviousMeta,0)-1),0).xy*weight;
                total+=weight;
            }
        }
        if(total>1e-3) {
            history/=total;
            meta/=total;
            // Variance clamp against the current neighbourhood (sky texels excluded). The upsampled signal is smooth
            // inside a tile and its noise lives between probes, so the 3x3 taps are one probe tile apart.
            vec3 mean=vec3(0.0);
            vec3 squared=vec3(0.0);
            float count=0.0;
            ivec2 currentSize=textureSize(Current,0);
            ivec2 center=min(ivec2(uv*vec2(currentSize)),currentSize-1);
            int stride=max(spTileSize()*currentSize.x/depthSize.x,1);
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
                float gamma=max(ScreenProbe.Weights.z,0.0);
                history.rgb=clamp(history.rgb,mean-gamma*sigma,mean+gamma*sigma);
            }
            historyLength=min(meta.x+1.0,float(max(ScreenProbe.Filter.y,1)));
            float alpha=1.0/historyLength;
            color=mix(history.rgb,current.rgb,alpha);
            validity=mix(meta.y,current.a,alpha);
        }
    }
    float confidence=clamp(validity,0.0,1.0)*clamp(historyLength/confidenceHistory,0.0,1.0);
    imageStore(Output,pixelOut,vec4(max(color,vec3(0.0)),confidence));
    imageStore(Meta,pixelOut,vec4(historyLength,validity,0.0,0.0));
}
