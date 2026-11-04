#version 450
// Ported from F:/projects/_/assets_save/gui/sprite.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 engine_Projection;
} data_UBO;
layout(location=0) in vec2 TexCoords;
layout(location=1) in vec4 Color;
layout(location=0) out vec4 color;

layout(set=0,binding=8) uniform sampler2D image;

void main() {
    color = texture(image, TexCoords);
}
