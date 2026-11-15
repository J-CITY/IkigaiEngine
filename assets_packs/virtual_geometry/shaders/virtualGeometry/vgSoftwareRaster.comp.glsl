#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
#include "vgSoftwareCommon.glsl"

layout(local_size_x = 64) in;
layout(std430, set = 0, binding = 7) readonly buffer Projected { ProjectedTriangle Items[]; } projected;
layout(std430, set = 0, binding = 8) readonly buffer Prefix { uint Count; } prefix;
layout(std430, set = 0, binding = 9) buffer Winners { uint64_t Keys[]; } winners;

void main() {
    uint slot = gl_GlobalInvocationID.x;
    uint tail = slot / MaxTriangles;
    uint batchDraw = software.Draw.y + tail;
    if (tail >= software.Draw.z || batchDraw < prefix.Count) return;
    ProjectedTriangle tri = projected.Items[slot];
    if (any(greaterThanEqual(tri.Bounds.xy, tri.Bounds.zw))) return;
    vec2 a = tri.FirstTwo.xy;
    vec2 b = tri.FirstTwo.zw;
    vec2 c = tri.ThirdAndDepth.xy;
    float winding = edge(a, b, c) > 0.0 ? 1.0 : -1.0;
    uint fields = ((software.Draw.x + batchDraw) << 7u) | (slot % MaxTriangles);
    uint64_t key = (uint64_t(floatBitsToUint(tri.ThirdAndDepth.z)) << 32u) | uint64_t(fields);
    for (int y = tri.Bounds.y; y < tri.Bounds.w; ++y) {
        for (int x = tri.Bounds.x; x < tri.Bounds.z; ++x) {
            vec2 samplePoint = vec2(x, y) + 0.5;
            if (winding * edge(a, b, samplePoint) > 0.0 && winding * edge(b, c, samplePoint) > 0.0
                && winding * edge(c, a, samplePoint) > 0.0) {
                atomicMin(winners.Keys[uint(y) * software.Extent.x + uint(x)], key);
            }
        }
    }
}
