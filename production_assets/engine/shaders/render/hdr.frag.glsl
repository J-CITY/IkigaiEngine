#version 450
// Ported from F:/projects/_/assets_save/gl/hdr.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float u_Exposure;
    float u_Gamma;
    bool u_UseHDR;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D u_Scene;

void main() {

    vec3 color = texture(u_Scene, TexCoords).rgb;

    if(data_UBO.u_UseHDR) {

        vec3 result = vec3(1.0) - exp(-color * data_UBO.u_Exposure);

        result = pow(result, vec3(1.0 / data_UBO.u_Gamma));

        FragColor = vec4(result, 1.0);
    }
    else {
        vec3 result = pow(color, vec3(1.0 / data_UBO.u_Gamma));
        FragColor = vec4(result, 1.0);
    }
}
