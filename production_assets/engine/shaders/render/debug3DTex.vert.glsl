#version 450
// Ported from F:/projects/_/assets_save/gl/debug3DTex.vs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float far_plane;
    int is3D;
    int isPerspective;
    int isRGB;
    int layer;
    float near_plane;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=0) out vec2 TexCoords;

void main() {
    TexCoords = aTexCoords;
    gl_Position = vec4(aPos, 1.0);
}
