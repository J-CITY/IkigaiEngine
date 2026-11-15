#version 450
// HZB mip N: conservative reduction of mip N-1 for the standard depth convention (larger depth is farther), so each
// texel stores the farthest depth (MAX) of every source texel that overlaps its footprint. The chain is exact:
// mip k is max(1, floor(width / 2^k)) wide. When the source size is odd the footprint of the last texels spans
// 3 source texels per axis; the range [floor(i*src/dst), ceil((i+1)*src/dst)) covers all of them (2 texels when even).
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
    float farthest=0.0;
    for(int y=first.y;y<last.y;++y) {
        for(int x=first.x;x<last.x;++x) {
            farthest=max(farthest,imageLoad(Input,ivec2(x,y)).r);
        }
    }
    imageStore(Output,pixel,vec4(farthest));
}
