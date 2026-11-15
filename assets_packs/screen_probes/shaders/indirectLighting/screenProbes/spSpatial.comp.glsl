#version 450
// Spatial probe filter: bilateral average of the (2r+1)^2 neighbouring probes weighted by plane distance (depth),
// normal similarity and a spatial Gaussian. The confidence is filtered with the same weights. Debug RAW copies.
#include "screenProbeCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D ProbeInfo;
layout(set=0,binding=11) uniform sampler2D Input;
layout(set=0,binding=12,rgba16f) uniform writeonly image2D Output;
bool probeSurface(ivec2 probe,vec2 size,out vec3 position,out vec3 normal) {
    ivec2 pixel;
    position=vec3(0.0);
    normal=vec3(0.0,0.0,1.0);
    if(!spProbePixel(ProbeInfo,probe,pixel))return false;
    position=spWorldPosition((vec2(pixel)+0.5)/size,texelFetch(Depth,pixel,0).r);
    normal=normalize(texelFetch(Normals,pixel,0).xyz);
    return true;
}
void main() {
    ivec2 probe=ivec2(gl_GlobalInvocationID.xy);
    ivec2 grid=imageSize(Output);
    if(any(greaterThanEqual(probe,grid)))return;
    vec4 center=texelFetch(Input,probe,0);
    int radius=clamp(ScreenProbe.Filter.z,0,3);
    vec2 size=vec2(textureSize(Depth,0));
    vec3 position,normal;
    if(spDebugMode()==SP_DEBUG_RAW||radius==0||!probeSurface(probe,size,position,normal)) {
        imageStore(Output,probe,center);
        return;
    }
    float planeScale=1.0/max(ScreenProbe.Weights.x*spViewDepth(position),1e-4);
    vec4 sum=vec4(0.0);
    float total=0.0;
    for(int y=-radius;y<=radius;++y) {
        for(int x=-radius;x<=radius;++x) {
            ivec2 neighbour=probe+ivec2(x,y);
            if(any(lessThan(neighbour,ivec2(0)))||any(greaterThanEqual(neighbour,grid)))continue;
            vec3 p,n;
            if(!probeSurface(neighbour,size,p,n))continue;
            vec4 value=texelFetch(Input,neighbour,0);
            float weight=exp(-0.5*float(x*x+y*y)/max(float(radius*radius),1.0));
            weight*=exp(-abs(dot(normal,p-position))*planeScale);
            weight*=pow(max(dot(normal,n),0.0),ScreenProbe.Weights.y);
            sum+=value*weight;
            total+=weight;
        }
    }
    imageStore(Output,probe,total>1e-6?sum/total:center);
}
