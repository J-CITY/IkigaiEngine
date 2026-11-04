#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { 
    vec3 Position;
} In;

layout(binding = 2) uniform vec4 color;

void main()
{
	result = color;
}