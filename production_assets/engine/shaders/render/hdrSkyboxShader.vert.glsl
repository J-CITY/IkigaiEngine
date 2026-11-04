#version 450
// Ported from F:/projects/_/assets_save/gl/hdrSkyboxShader.vs.glsl

layout(location=0) in vec3 aPos;

layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4    View;
    mat4    Projection;
    vec3    ViewPos;
    float   Time;
    vec2    ViewportSize;
    float    FPS;
    int      FrameCount;
} engine_UBO;

layout(push_constant) uniform ModelPush { mat4 model; } engine_Model;
layout(location=0) out vec3 WorldPos;

void main() {
    WorldPos = aPos;
	mat4 rotView = mat4(mat3(engine_UBO.View));
	vec4 clipPos = engine_UBO.Projection * rotView * vec4(WorldPos, 1.0);
	gl_Position = clipPos.xyww;

}