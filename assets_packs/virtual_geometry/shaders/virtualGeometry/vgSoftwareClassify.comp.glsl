#version 450
#extension GL_GOOGLE_include_directive : require
#include "vgSoftwareCommon.glsl"

layout(local_size_x = 64) in;
layout(std140, set = 0, binding = 0) uniform EngineUBO {
    mat4 View; mat4 Projection; vec3 ViewPos; float Time;
    vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;

struct VgInstance { mat4 World; mat4 PreviousWorld; mat4 Normal; vec4 Params; };
struct VgCluster {
    uint IndexOffset; uint IndexCount; uint MaterialSlot; uint GroupId;
    uint LodLevel; uint Flags; float Error; float ParentError;
    vec4 Bounds; vec4 LodBounds; vec4 ParentLodBounds; vec4 Cone;
};
layout(std430, set = 0, binding = 2) readonly buffer VertexWords { uint Words[]; } vertices;
layout(std430, set = 0, binding = 3) readonly buffer IndexWords { uint Items[]; } indices;
layout(std430, set = 0, binding = 4) readonly buffer Clusters { VgCluster Items[]; } clusters;
layout(std430, set = 0, binding = 5) readonly buffer DrawRecords { uvec4 Items[]; } draws;
layout(std430, set = 0, binding = 6) readonly buffer Instances { VgInstance Items[]; } instances;
layout(std430, set = 0, binding = 7) writeonly buffer Projected { ProjectedTriangle Items[]; } projected;
layout(std430, set = 0, binding = 8) buffer Prefix { uint Count; } prefix;

shared uint unsafeDraw;

// One full minimum Vulkan subpixel step covers endpoint quantization.
// Admission separately bounds projection/viewport rounding per vertex.
const float ProjectionGuard = 0.03125;
const float PositionGuard = 0.0625 + ProjectionGuard;

vec4 absoluteTransform(mat4 m, vec4 v) {
    return abs(m[0]) * v.x + abs(m[1]) * v.y + abs(m[2]) * v.z + abs(m[3]) * v.w;
}
float edgeGuard(vec2 a, vec2 b, vec2 p, float radius) {
    vec2 d = abs(b - a);
    vec2 r = abs(p - a);
    // Bound cross(d + deltaD, r + deltaR) - cross(d, r).
    // An endpoint moves by radius, an edge vector by at most 2*radius.
    return radius * (d.x + d.y) + 2.0 * radius * (r.x + r.y) + 4.0 * radius * radius + 0.001;
}

bool classify(uint startIndex, mat4 world, out ProjectedTriangle tri) {
    vec2 point[3];
    uint depthBits[3];
    for (uint vertex = 0u; vertex < 3u; ++vertex) {
        uint index = indices.Items[startIndex + vertex];
        if (index >= vertices.Words.length() / 7u) return false;
        uint first = index * 7u;
        vec3 position = vec3(uintBitsToFloat(vertices.Words[first]), uintBitsToFloat(vertices.Words[first + 1u]),
            uintBitsToFloat(vertices.Words[first + 2u]));
        vec4 local = vec4(position, 1.0);
        precise vec4 worldPosition = world * local;
        precise mat4 viewProjection = engine_UBO.Projection * engine_UBO.View;
        precise vec4 clip = viewProjection * worldPosition;
        // Match HW depth without relying on its perspective divide rounding.
        // The initial prototype admits only unit-w, constant-depth triangles.
        if (any(isnan(clip)) || any(isinf(clip)) || clip.w != 1.0) return false;
        vec3 ndc = clip.xyz;
        // Absolute matrix products bound cancellation sensitivity. Inflate
        // the FP32 error budget across the transforms and perspective divide
        // before using the fixed pixel-space coverage guard.
        const float FloatEpsilon = 1.1920928955078125e-7;
        vec4 magnitude = absoluteTransform(engine_UBO.Projection,
            absoluteTransform(engine_UBO.View, absoluteTransform(world, abs(local))));
        vec4 clipError = (64.0 * FloatEpsilon) * magnitude;
        float minW = clip.w - clipError.w;
        if (!(minW > 0.0)) return false;
        vec3 ndcError = (clipError.xyz + abs(ndc) * clipError.w) / minW + 4.0 * FloatEpsilon * abs(ndc);
        vec2 extent = vec2(software.Extent.xy);
        vec2 pixelError = (0.5 * ndcError.xy + vec2(4.0 * FloatEpsilon)) * extent;
        if (!all(lessThanEqual(pixelError, vec2(ProjectionGuard)))) return false;
        vec3 clipGuard = max(ndcError, vec3(1e-5));
        if (!all(lessThan(abs(ndc.xy) + clipGuard.xy, vec2(1.0))) ||
            !(ndc.z > clipGuard.z && ndc.z < 1.0 - clipGuard.z)) return false;
        point[vertex] = (ndc.xy * vec2(0.5, -0.5) + 0.5) * extent;
        depthBits[vertex] = floatBitsToUint(ndc.z == 0.0 ? 0.0 : ndc.z);
    }
    if (depthBits[0] != depthBits[1] || depthBits[0] != depthBits[2]) return false;
    if (abs(edge(point[0], point[1], point[2])) <= edgeGuard(point[0], point[1], point[2], 2.0 * PositionGuard)) return false;

    vec2 low = min(point[0], min(point[1], point[2])) - PositionGuard;
    vec2 high = max(point[0], max(point[1], point[2])) + PositionGuard;
    ivec2 begin = max(ivec2(ceil(low - 0.5)), ivec2(0));
    ivec2 end = min(ivec2(floor(high - 0.5)) + 1, ivec2(software.Extent.xy));
    if (any(greaterThan(end - begin, ivec2(8)))) return false;

    // Reject the whole draw if either endpoint quantization or arithmetic
    // uncertainty could move a tested pixel center across any edge.
    for (int y = begin.y; y < end.y; ++y) {
        for (int x = begin.x; x < end.x; ++x) {
            vec2 samplePoint = vec2(x, y) + 0.5;
            for (uint i = 0u; i < 3u; ++i) {
                vec2 a = point[i];
                vec2 b = point[(i + 1u) % 3u];
                if (abs(edge(a, b, samplePoint)) <= edgeGuard(a, b, samplePoint, PositionGuard)) return false;
            }
        }
    }
    tri = ProjectedTriangle(vec4(point[0], point[1]), vec4(point[2], uintBitsToFloat(depthBits[0]), 0.0),
        ivec4(begin, end));
    return true;
}

void main() {
    uint lane = gl_LocalInvocationID.x;
    uint tail = gl_WorkGroupID.x;
    if (lane == 0u) unsafeDraw = 0u;
    barrier();

    uint batchDraw = software.Draw.y + tail;
    uint drawIndex = software.Draw.x + batchDraw;
    bool safe = false;
    uint triangles = 0u;
    uint indexStart = 0u;
    uint instance = 0u;
    if (drawIndex < draws.Items.length()) {
        uvec2 record = draws.Items[drawIndex].xy;
        if (record.x < instances.Items.length() && record.y < clusters.Items.length()) {
            VgCluster cluster = clusters.Items[record.y];
            indexStart = cluster.IndexOffset;
            uint count = cluster.IndexCount;
            uint capacity = indices.Items.length();
            safe = count > 0u && count <= MaxTriangles * 3u && count % 3u == 0u
                && indexStart <= capacity && count <= capacity - indexStart;
            triangles = safe ? count / 3u : 0u;
            instance = record.x;
        }
    }
    for (uint primitive = lane; primitive < MaxTriangles; primitive += gl_WorkGroupSize.x) {
        uint slot = tail * MaxTriangles + primitive;
        ProjectedTriangle tri = ProjectedTriangle(vec4(0.0), vec4(0.0), ivec4(0));
        if (primitive < triangles && !classify(indexStart + primitive * 3u, instances.Items[instance].World, tri)) {
            atomicOr(unsafeDraw, 1u);
        }
        projected.Items[slot] = tri;
    }
    if (!safe) atomicOr(unsafeDraw, 1u);
    barrier();
    if (lane == 0u && unsafeDraw != 0u) atomicMax(prefix.Count, batchDraw + 1u);
}
