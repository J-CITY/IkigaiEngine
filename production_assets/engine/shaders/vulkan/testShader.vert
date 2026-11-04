#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(binding = 1) uniform _ubo
{
	mat4 projection;
	mat4 view;
	mat4 model;
} ubo;

layout(location = 0) out struct { vec2 TexCoord; } Out;
out gl_PerVertex { vec4 gl_Position; };

void main()
{
	Out.TexCoord = aTexCoord;
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
}
