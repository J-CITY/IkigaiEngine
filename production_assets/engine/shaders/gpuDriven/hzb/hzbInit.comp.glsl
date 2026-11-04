#version 450
// HZB mip 0: exact copy of the scene depth (D32, standard depth: 0 = near, 1 = far, larger is farther).
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9,r32f) uniform writeonly image2D Output;
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(pixel,imageSize(Output))))return;
    ivec2 depthSize=textureSize(Depth,0);
    float depth=texelFetch(Depth,min(pixel,depthSize-1),0).r;
    imageStore(Output,pixel,vec4(depth));
}
