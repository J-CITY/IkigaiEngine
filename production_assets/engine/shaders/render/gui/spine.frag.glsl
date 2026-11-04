#version 450
// Ported from F:/projects/_/assets_save/gui/spine.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 u_engine_model;
    mat4 u_engine_projection;
} data_UBO;
layout(location=0) in vec2 TexCoords;
layout(location=1) in vec4 OutColor;
layout(location=0) out vec4 color;

layout(set=0,binding=8) uniform sampler2D image;

void main() {
    
    color = OutColor * texture(image, TexCoords);

}
