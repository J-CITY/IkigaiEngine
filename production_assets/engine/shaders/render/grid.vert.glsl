#version 450
// Ported from F:/projects/_/assets_save/gl/grid.vs.glsl

layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;

#ifdef GL_FRAGMENT_PRECISION_HIGH
  precision highp float;
#else
  precision mediump float;
#endif
layout(location=0) out vec3 near;
layout(location=1) out vec3 far;

layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4    View;
    mat4    Projection;
    vec3    ViewPos;
    float   Time;
    vec2    ViewportSize;
    float    FPS;
    int      FrameCount;
} engine_UBO;

layout(push_constant) uniform ModelPush { mat4 model; } engine_Model;

vec3 unproject_point(float x, float y, float z) {
    mat4 inv = inverse(engine_UBO.Projection * engine_UBO.View);
    vec4 unproj_point = inv * vec4(x, y, z, 1.f);
    return unproj_point.xyz / unproj_point.w;
}

void main() {
    vec2 p = aPos.xy;
    near = unproject_point(p.x, p.y, -1.f);
    far  = unproject_point(p.x, p.y,  1.f);
    gl_Position = vec4(aPos.xy, 0.0f, 1.0f);
}
