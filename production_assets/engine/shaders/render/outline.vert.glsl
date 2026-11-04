#version 450
// Ported from shared fullscreen

layout(std140,set=0,binding=4) uniform PassParameters {
    vec2 gamma;
    vec2 nearFar;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=0) out vec2 TexCoords;
void main(){TexCoords=aTexCoords;gl_Position=vec4(aPos,1.0);}
