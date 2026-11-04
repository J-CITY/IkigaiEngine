#version 450
// Ported from F:/projects/_/assets_save/gl/renderToScreen.vs.glsl

layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=0) out vec2 outUV;

void main() {
    outUV = aTexCoords;
    gl_Position = vec4(aPos, 1.0);
}
