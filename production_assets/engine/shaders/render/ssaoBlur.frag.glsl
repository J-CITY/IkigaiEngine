#version 450
// Ported from F:/projects/_/assets_save/gl/ssaoBlur.fs.glsl

layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D gInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(gInput, 0));
    float result = 0.0;
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(gInput, TexCoords + offset).r;
        }
    }
    FragColor = vec4(result / (4.0 * 4.0));
}
