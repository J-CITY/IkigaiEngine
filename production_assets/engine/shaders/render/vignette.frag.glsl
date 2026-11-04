#version 450
// Ported from F:/projects/_/assets_save/gl/vignette.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float u_Intensity;
    float u_Radius;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D u_Scene;

vec3 applyVignette(vec3 color, vec2 texcoord, float radius, float intensity) {
    texcoord *= 1.0f - texcoord.yx;
    float vig = texcoord.x * texcoord.y * intensity;
    vig = pow(vig, radius);
    return color * min(vig, 1.0f);
}

void main() {
    vec3 color = texture(u_Scene, TexCoords).rgb;
    
    FragColor = vec4(color, 1.0f);
}
