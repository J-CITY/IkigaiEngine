#version 450 core

layout(location = 0) in vec3 aPosition;

layout(binding = 0) uniform EngineUBO
{
	mat4 View;
	mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} ubo;

layout(push_constant) uniform Constants {
    mat4 model;
} engine_Model;

layout(location = 0) out struct { 
    vec3 Position;
} Out;

out gl_PerVertex { 
    vec4 gl_Position; 
};

void main()
{
	Out.Position = vec3(engine_Model.model * vec4(aPosition, 1.0));
	gl_Position = ubo.Projection * ubo.View * engine_Model.model * vec4(aPosition, 1.0);
}