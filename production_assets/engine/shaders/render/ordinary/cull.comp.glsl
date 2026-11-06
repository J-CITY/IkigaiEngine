#version 450

layout(local_size_x = 64) in;

struct InstanceTransform {
    mat4 Current;
    mat4 Previous;
    vec4 Tint;
};

struct InstanceCandidate {
    InstanceTransform Transform;
    vec4 ModelBounds;
    vec4 MeshBounds;
    uvec4 Lod;
    vec4 LocalBounds;
};

layout(std430, set = 0, binding = 0) readonly buffer CandidateBuffer {
    InstanceCandidate Rows[];
} Candidates;

layout(std430, set = 0, binding = 1) writeonly buffer VisibleBuffer {
    InstanceTransform Rows[];
} Visible;

layout(std430, set = 0, binding = 2) buffer IndirectBuffer {
    uint IndexCount;
    uint InstanceCount;
    uint FirstIndex;
    int VertexOffset;
    uint FirstInstance;
} Arguments;

layout(std140, set = 0, binding = 3) uniform CullConstants {
    vec4 Planes[6];
    uvec4 Counts;
    vec4 OcclusionViewProjection[4];
} Cull;

layout(std430, set = 0, binding = 4) readonly buffer LodSelectionBuffer {
    uvec2 Selected[];
} LodSelection;

layout(set = 0, binding = 5) uniform sampler2D Hzb;

layout(std430, set = 0, binding = 6) buffer OcclusionBuffer {
    uint Rejected[];
} Occlusion;

#include "../../gpuDriven/hzb/hzbOcclusion.glsl"

bool visible(vec4 sphere) {
    if (sphere.w < 0.0) {
        return true;
    }
    for (int plane = 0; plane < 6; ++plane) {
        if (dot(Cull.Planes[plane], vec4(sphere.xyz, 1.0)) <= -sphere.w) {
            return false;
        }
    }
    return true;
}

vec4 worldBounds(InstanceCandidate instance) {
    const mat4 world = instance.Transform.Current;
    const vec3 x = world[0].xyz;
    const vec3 y = world[1].xyz;
    const vec3 z = world[2].xyz;
    // The maximum absolute row sum of the Gram matrix bounds the squared
    // largest scale, including transforms with shear.
    const vec3 crossTerms = abs(vec3(dot(x, y), dot(y, z), dot(x, z)));
    const vec3 bounds = vec3(dot(x, x), dot(y, y), dot(z, z)) + crossTerms.xyz + crossTerms.zxy;
    const float radius = instance.LocalBounds.w * sqrt(max(max(bounds.x, bounds.y), bounds.z)) * 1.000001;
    const vec3 center = (world * vec4(instance.LocalBounds.xyz, 1.0)).xyz;
    return vec4(center, radius);
}

bool occluded(InstanceCandidate instance) {
    const vec4 sphere = worldBounds(instance);
    return hzbSphereOccluded(Hzb, Cull.OcclusionViewProjection, sphere.xyz, sphere.w, false);
}

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index >= Cull.Counts.x) {
        return;
    }
    const InstanceCandidate instance = Candidates.Rows[index];
    const uint phase = Cull.Counts.y;
    if (phase == 3u) {
        if (instance.ModelBounds.w < -1.0 || instance.Lod.y != 0xffffffffu) {
            return;
        }
        const vec4 sphere = worldBounds(instance);
        if (sphere.w > 0.0 && !visible(sphere)) {
            return;
        }
    } else if (phase == 2u || phase == 4u) {
        if (Occlusion.Rejected[index] == 0u || (phase == 2u && occluded(instance))) {
            return;
        }
    } else {
        if (phase == 1u) {
            Occlusion.Rejected[index] = 0u;
        }
        if (instance.ModelBounds.w < -1.0 || !visible(instance.ModelBounds) || !visible(instance.MeshBounds)) {
            return;
        }
        if (instance.Lod.x != 0xffffffffu && LodSelection.Selected[instance.Lod.x].y != instance.Lod.y) {
            return;
        }
        if (phase == 1u && occluded(instance)) {
            Occlusion.Rejected[index] = 1u;
            return;
        }
    }
    const uint slot = atomicAdd(Arguments.InstanceCount, 1);
    Visible.Rows[slot] = instance.Transform;
}
