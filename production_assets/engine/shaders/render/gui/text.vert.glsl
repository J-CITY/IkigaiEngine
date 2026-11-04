#version 450
// Ported from F:/projects/_/assets_save/gui/text.vs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 u_engine_projection;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=2) in vec4 aColor;
layout(location=0) out vec2 TexCoords;
layout(location=1) out vec4 Color;

void main() {
    TexCoords = aTexCoords;
    Color = aColor;
    gl_Position = data_UBO.u_engine_projection * vec4(aPos.xyz, 1.0);
    
}