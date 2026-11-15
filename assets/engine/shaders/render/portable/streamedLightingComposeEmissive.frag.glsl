#version 450
// streamedLightingCompose with the opt-in Emissive G-buffer target added to the lit scene (linear HDR).
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
layout(set=0,binding=8) uniform sampler2D Indirect;
layout(set=0,binding=9) uniform sampler2D Direct;
layout(set=0,binding=10) uniform sampler2D Emissive;

void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    Color=vec4(texelFetch(Indirect,pixel,0).rgb+texelFetch(Direct,pixel,0).rgb+texelFetch(Emissive,pixel,0).rgb,1);
}
