#version 450
// Ported from F:/projects/_/assets_save/gl/bloom.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    bool u_UseBloom;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=9) uniform sampler2D u_Scene;
layout(set=0,binding=8) uniform sampler2D u_BloomBlur;

void main() {
    vec3 color = texture(u_Scene, TexCoords).rgb;
    vec3 bloomColor = texture(u_BloomBlur, TexCoords).rgb;
    if(data_UBO.u_UseBloom) {
        color += bloomColor;
    }
    FragColor = vec4(color, 1.0);
}
