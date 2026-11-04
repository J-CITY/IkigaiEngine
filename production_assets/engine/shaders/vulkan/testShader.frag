#version 450 core

layout(location = 0) out vec4 result;
layout(location = 0) in struct { vec2 TexCoord; } In;


layout(binding = 0) uniform sampler2D sTexture;

void main()
{
	if (In.TexCoord.x > 1.0 || In.TexCoord.x < 0.0) {
		result = vec4(1.0);
	}
	else if (In.TexCoord.y > 1.0 || In.TexCoord.y < 0.0) {
		result = vec4(0.0);
	}
	else {
		result = texture(sTexture, In.TexCoord).rgba;
	}
}
