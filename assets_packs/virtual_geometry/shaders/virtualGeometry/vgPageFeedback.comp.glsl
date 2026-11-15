#version 450
layout(local_size_x = 64) in;

layout(std140, set = 0, binding = 0) uniform VgFeedbackView {
    vec4 WorldRows[3];
    vec4 CameraScale;
    vec4 Projection;
    uvec4 Counts;
} engine_VgFeedbackView;

struct CoarseInput {
    vec4 Bounds;
    float Error;
    uint Candidate;
    uint Reserved0;
    uint Reserved1;
};
layout(std430, set = 0, binding = 1) readonly buffer VgFeedbackCoarse {
    CoarseInput Items[];
} engine_VgFeedbackCoarse;
layout(std430, set = 0, binding = 2) buffer VgFeedbackPriorities {
    uint Items[];
} engine_VgFeedbackPriorities;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= engine_VgFeedbackView.Counts.x) return;
    CoarseInput coarse = engine_VgFeedbackCoarse.Items[index];
    const float maxPriority = 3.402823466e+38;
    float projected = maxPriority;
    if (coarse.Error < maxPriority) {
        float scale = engine_VgFeedbackView.CameraScale.w;
        vec4 projection = engine_VgFeedbackView.Projection;
        projected = coarse.Error * scale * projection.x;
        if (projection.w < 0.5) {
            vec4 local = vec4(coarse.Bounds.xyz, 1.0);
            vec3 center = vec3(dot(engine_VgFeedbackView.WorldRows[0], local), dot(engine_VgFeedbackView.WorldRows[1], local),
                dot(engine_VgFeedbackView.WorldRows[2], local));
            float distance = length(center - engine_VgFeedbackView.CameraScale.xyz) - coarse.Bounds.w * scale;
            projected /= max(distance, projection.y);
        }
        projected -= projection.z;
    }
    if (projected > 0.0) {
        // Nonnegative finite float bit patterns sort in the same order as the
        // floats. Each legal transition keeps its highest coarse member score.
        atomicMax(engine_VgFeedbackPriorities.Items[coarse.Candidate], floatBitsToUint(min(projected, maxPriority)));
    }
}
