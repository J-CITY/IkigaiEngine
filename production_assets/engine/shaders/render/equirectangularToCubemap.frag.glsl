#version 450
// Ported from F:/projects/_/assets_save/gl/equirectangularToCubemap.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 projection;
    mat4 view;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec3 WorldPos;

layout(set=0,binding=8) uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main() {
    vec2 uv = SampleSphericalMap(normalize(WorldPos));
    vec3 color = texture(equirectangularMap, uv).rgb;
    FragColor = vec4(color, 1.0);
}
