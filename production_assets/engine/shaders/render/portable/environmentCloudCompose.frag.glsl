#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(set=0,binding=10) uniform sampler2D Clouds;
layout(set=0,binding=11) uniform sampler2D LowDepth;

void main() {
vec2 size=vec2(textureSize(Clouds,0)),q=UV*size-.5,base=floor(q),fraction=fract(q);
float depth=texture(Depth,UV).r,total=0;vec4 cloud=vec4(0);
for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
vec2 uv=(base+vec2(x,y)+.5)/size;float other=texture(LowDepth,uv).r;
float w=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y)*exp(-abs(depth-other)*4096);
cloud+=texture(Clouds,uv)*w;total+=w;
}
cloud=total>1e-5 ? cloud/total : vec4(0,0,0,1);
Color=vec4(cloud.rgb+clamp(cloud.a,0,1)*texture(Scene,UV).rgb,1);
}
