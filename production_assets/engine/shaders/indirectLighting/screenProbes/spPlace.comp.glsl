#version 450
// Probe placement: one probe per TileSize^2 tile, jittered per frame. Picks a pixel with valid depth inside the tile
// (four jittered candidates, then a coarse scan); sky / empty tiles are invalid. ProbeInfo = (offset in tile, valid, 0).
// Depth and normal are read back from the G-buffer at that pixel by the later stages (exact, no fp16 depth).
#include "screenProbeCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9,rgba16f) uniform writeonly image2D ProbeInfo;
void main() {
    ivec2 probe=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(probe,imageSize(ProbeInfo))))return;
    int tile=spTileSize();
    ivec2 size=textureSize(Depth,0);
    ivec2 origin=probe*tile;
    ivec2 extent=min(ivec2(tile),size-origin);
    if(any(lessThanEqual(extent,ivec2(0)))) {
        imageStore(ProbeInfo,probe,vec4(0.0));
        return;
    }
    uint seed=spHash(uint(probe.x)*73856093u^uint(probe.y)*19349663u);
    vec2 jitter=spR2(uint(engine_UBO.FrameCount));
    ivec2 chosen=ivec2(-1);
    for(int k=0;k<4&&chosen.x<0;++k) {
        vec2 u=fract(jitter+vec2(spHashFloat(seed+uint(2*k)),spHashFloat(seed+uint(2*k+1))));
        ivec2 offset=min(ivec2(u*vec2(extent)),extent-1);
        if(texelFetch(Depth,origin+offset,0).r<SP_SKY_DEPTH) {
            chosen=offset;
        }
    }
    int stride=max(tile/4,1);
    for(int y=stride/2;y<extent.y&&chosen.x<0;y+=stride) {
        for(int x=stride/2;x<extent.x&&chosen.x<0;x+=stride) {
            if(texelFetch(Depth,origin+ivec2(x,y),0).r<SP_SKY_DEPTH) {
                chosen=ivec2(x,y);
            }
        }
    }
    imageStore(ProbeInfo,probe,chosen.x<0?vec4(0.0):vec4(vec2(chosen),1.0,0.0));
}
