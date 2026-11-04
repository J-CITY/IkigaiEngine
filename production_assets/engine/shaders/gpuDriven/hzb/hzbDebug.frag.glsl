#version 450
// HZB debug view: shows one mip of the depth pyramid as grey (depth ^ Power, larger = farther = brighter).
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D HZB;
layout(std140,set=0,binding=4) uniform Parameters { int Mip; float Power; } data_UBO;
void main() {
    int mip=clamp(data_UBO.Mip,0,textureQueryLevels(HZB)-1);
    ivec2 size=textureSize(HZB,mip);
    ivec2 pixel=min(ivec2(UV*vec2(size)),size-1);
    float depth=texelFetch(HZB,pixel,mip).r;
    Color=vec4(vec3(pow(clamp(depth,0.0,1.0),data_UBO.Power)),1.0);
}
