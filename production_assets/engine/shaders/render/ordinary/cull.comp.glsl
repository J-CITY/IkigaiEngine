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
} Cull;

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

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index >= Cull.Counts.x) {
        return;
    }
    const InstanceCandidate instance = Candidates.Rows[index];
    if (instance.ModelBounds.w < -1.0 || !visible(instance.ModelBounds) || !visible(instance.MeshBounds)) {
        return;
    }
    const uint slot = atomicAdd(Arguments.InstanceCount, 1);
    Visible.Rows[slot] = instance.Transform;
}
