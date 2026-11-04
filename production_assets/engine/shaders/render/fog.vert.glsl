#version 450
// Ported from shared fullscreen
struct FogParameters {
	vec3 color;
	float linearStart;
	float linearEnd;
	float density;
	
	int equation;
	bool isEnabled;
};
layout(std140,set=0,binding=4) uniform PassParameters {
    FogParameters u_engine_FogParams;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=0) out vec2 TexCoords;
void main(){TexCoords=aTexCoords;gl_Position=vec4(aPos,1.0);}
