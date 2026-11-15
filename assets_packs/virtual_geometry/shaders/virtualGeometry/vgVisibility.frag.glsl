#version 450
layout(location=0) flat in int VisibilityDraw;
layout(location=0) out ivec2 VisibilityId;

void main() {
    VisibilityId = ivec2(VisibilityDraw, gl_PrimitiveID);
}
