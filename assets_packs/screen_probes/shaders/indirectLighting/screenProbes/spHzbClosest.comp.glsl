#version 450
// Closest-depth pyramid for screen tracing. Same chain and footprint rules as the M1 HZB reduction
// (gpuDriven/hzb/hzbReduce.comp.glsl) but MIN instead of MAX: a ray segment in front of a texel's closest depth
// cannot hit anything under that texel, so the march may skip it. The M1 HZB keeps the farthest depth for culling.
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=9,r32f) uniform writeonly image2D Output;
layout(set=0,binding=10,r32f) uniform readonly image2D Input;
layout(std140,set=0,binding=11) uniform MipInfoBlock {uvec4 Info;} MipInfo;
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    ivec2 dstSize=ivec2(MipInfo.Info.yz);
    if(any(greaterThanEqual(pixel,dstSize)))return;
    ivec2 srcSize=imageSize(Input);
    ivec2 first=(pixel*srcSize)/dstSize;
    ivec2 last=min(((pixel+1)*srcSize+dstSize-1)/dstSize,srcSize);
    float closest=1.0;
    for(int y=first.y;y<last.y;++y) {
        for(int x=first.x;x<last.x;++x) {
            closest=min(closest,imageLoad(Input,ivec2(x,y)).r);
        }
    }
    imageStore(Output,pixel,vec4(closest));
}
