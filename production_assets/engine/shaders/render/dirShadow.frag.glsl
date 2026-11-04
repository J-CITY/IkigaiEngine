#version 450
// Ported from F:/projects/_/assets_save/gl/dirShadow.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 lightSpaceMatrices[16];
} data_UBO;

void main(){}