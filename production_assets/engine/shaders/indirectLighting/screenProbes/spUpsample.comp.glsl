#version 450
// Upsample probe irradiance to the Indirect resolution: bilateral weights over the 4 nearest probes (bilinear in the
// tile grid x plane distance x normal similarity). Pixels whose 4 probes all reject fall back to the 3x3 tile ring.
// Output: rgb = irradiance / pi of the probes, a = weighted probe confidence (0 when nothing matches: IBL fallback).
#include "screenProbeCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D ProbeInfo;
layout(set=0,binding=11) uniform sampler2D Irradiance;
layout(set=0,binding=12,rgba16f) uniform writeonly image2D Output;

float probeWeight(ivec2 probe,ivec2 grid,vec2 size,vec3 position,vec3 normal,float planeScale) {
    ivec2 pixel;
    if(any(lessThan(probe,ivec2(0)))||any(greaterThanEqual(probe,grid))||!spProbePixel(ProbeInfo,probe,pixel))return 0.0;
    vec3 p=spWorldPosition((vec2(pixel)+0.5)/size,texelFetch(Depth,pixel,0).r);
    vec3 n=normalize(texelFetch(Normals,pixel,0).xyz);
    return exp(-abs(dot(normal,p-position))*planeScale)*pow(max(dot(normal,n),0.0),ScreenProbe.Weights.y);
}

void main() {
    ivec2 pixelOut=ivec2(gl_GlobalInvocationID.xy);
    ivec2 outSize=imageSize(Output);
    if(any(greaterThanEqual(pixelOut,outSize)))return;
    vec2 size=vec2(textureSize(Depth,0));
    vec2 uv=(vec2(pixelOut)+0.5)/vec2(outSize);
    ivec2 pixel=min(ivec2(uv*size),ivec2(size)-1);
    float depth=texelFetch(Depth,pixel,0).r;
    if(depth>=SP_SKY_DEPTH) {
        imageStore(Output,pixelOut,vec4(0.0));
        return;
    }
    vec3 position=spWorldPosition((vec2(pixel)+0.5)/size,depth);
    vec3 normal=normalize(texelFetch(Normals,pixel,0).xyz);
    float planeScale=1.0/max(ScreenProbe.Weights.x*spViewDepth(position),1e-4);
    ivec2 grid=textureSize(ProbeInfo,0);
    float tile=float(spTileSize());
    vec2 g=(vec2(pixel)+0.5)/tile-0.5;
    ivec2 base=ivec2(floor(g));
    vec2 f=g-vec2(base);
    vec4 sum=vec4(0.0);
    float total=0.0;
    for(int y=0;y<2;++y) {
        for(int x=0;x<2;++x) {
            ivec2 probe=base+ivec2(x,y);
            float weight=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y)+1e-3;
            weight*=probeWeight(probe,grid,size,position,normal,planeScale);
            if(weight<=0.0)continue;
            sum+=texelFetch(Irradiance,probe,0)*weight;
            total+=weight;
        }
    }
    if(total<1e-4) {
        ivec2 own=ivec2(vec2(pixel)/tile);
        for(int y=-1;y<=1;++y) {
            for(int x=-1;x<=1;++x) {
                ivec2 probe=own+ivec2(x,y);
                float weight=probeWeight(probe,grid,size,position,normal,planeScale);
                if(weight<=0.0)continue;
                sum+=texelFetch(Irradiance,probe,0)*weight;
                total+=weight;
            }
        }
    }
    vec4 result=total>1e-6?sum/total:vec4(0.0);
    // Weak matches (corners, thin features) lose confidence so compose leans on the IBL fallback.
    result.a*=clamp(total*4.0,0.0,1.0);
    imageStore(Output,pixelOut,result);
}
