#version 450
// Ported from F:/projects/_/assets_save/gl/colorGrading.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec3 u_ChannelB;
    vec3 u_ChannelG;
    vec3 u_ChannelR;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D u_Scene;

void main() {
    vec3 inputColor = texture(u_Scene, TexCoords).rgb;
    vec3 outputColor = data_UBO.u_ChannelR * inputColor.r + data_UBO.u_ChannelG * inputColor.g + data_UBO.u_ChannelB * inputColor.b;
    outputColor = min(vec3(1), outputColor);
    FragColor = vec4(outputColor, 1.0f);
}
