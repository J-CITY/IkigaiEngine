#version 450
// Ported from F:/projects/_/assets_save/gl/standardLines.fs.glsl

layout(location=0) out vec4 FRAGMENT_COLOR;

vec3 color = vec3(1, 0, 0);

void main() {
    FRAGMENT_COLOR = vec4(color, 1);
}
