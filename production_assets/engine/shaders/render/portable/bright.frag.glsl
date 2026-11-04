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
layout(std140,set=0,binding=4) uniform Parameters { float Threshold; } data_UBO;
void main() { vec3 c=texture(Scene,UV).rgb; float l=max(max(c.r,c.g),c.b); Color=vec4(c*max(l-data_UBO.Threshold,0)/max(l,1e-5),1); }