#version 450
// Ported from F:/projects/_/assets_save/gl/prefilterShader.vs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 projection;
    float roughness;
    mat4 view;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=0) out vec3 WorldPos;

void main() {
    WorldPos = aPos;  
    gl_Position =  data_UBO.projection * data_UBO.view * vec4(WorldPos, 1.0);
}
