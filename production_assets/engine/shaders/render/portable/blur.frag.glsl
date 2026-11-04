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
layout(std140,set=0,binding=4) uniform Parameters { vec2 Direction; } data_UBO;

void main() { vec2 stepUV=data_UBO.Direction/vec2(textureSize(Scene,0)); vec3 c=texture(Scene,UV).rgb*0.227027;
c+=(texture(Scene,UV+stepUV*1.384615).rgb+texture(Scene,UV-stepUV*1.384615).rgb)*0.316216;
c+=(texture(Scene,UV+stepUV*3.230769).rgb+texture(Scene,UV-stepUV*3.230769).rgb)*0.070270; Color=vec4(c,1); }
