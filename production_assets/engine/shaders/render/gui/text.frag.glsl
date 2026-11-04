#version 450
// Ported from F:/projects/_/assets_save/gui/text.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 u_engine_projection;
} data_UBO;
layout(location=0) in vec2 TexCoords;
layout(location=1) in vec4 Color;
layout(location=0) out vec4 Out;

layout(set=0,binding=8) uniform sampler2D u_engine_text;

void main() {
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(u_engine_text, TexCoords).r);
    Out = Color * sampled;
}
