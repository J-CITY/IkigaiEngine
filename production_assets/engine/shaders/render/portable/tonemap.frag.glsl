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
layout(set=0,binding=9) uniform sampler2D Bloom;
layout(std140,set=0,binding=4) uniform Parameters { float BloomStrength; float Exposure; float Gamma; } data_UBO;

void main() { vec3 c=(texture(Scene,UV).rgb+texture(Bloom,UV).rgb*data_UBO.BloomStrength)*data_UBO.Exposure;
c=clamp((c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14),0,1); Color=vec4(pow(c,vec3(1/max(data_UBO.Gamma,0.01))),1); }
