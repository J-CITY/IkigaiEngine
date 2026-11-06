#version 450

layout(local_size_x = 64) in;

struct LodObject {
    vec4 Position;
    vec4 Distances;
    uvec4 State; // level count and slot generation
};

layout(std430, set = 0, binding = 0) readonly buffer LodObjectBuffer {
    LodObject Objects[];
} LodInput;

layout(std430, set = 0, binding = 1) buffer LodSelectionBuffer {
    uvec2 Selected[];
} LodSelection;

layout(std140, set = 0, binding = 2) uniform LodView {
    vec4 Position;
    uvec4 Counts;
} View;

float hysteresis(LodObject object, uint boundary) {
    const float distance = object.Distances[boundary];
    const float previous = boundary == 0u ? 0.0 : object.Distances[boundary - 1u];
    const float next = boundary + 2u == object.State.x ? distance * 2.0 : object.Distances[boundary + 1u];
    return min(distance * 0.05, min(distance - previous, next - distance) * 0.25);
}

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index >= View.Counts.x) {
        return;
    }
    const LodObject object = LodInput.Objects[index];
    if (object.State.x == 0u) {
        return;
    }
    const float distance = length(object.Position.xyz - View.Position.xyz);
    const uvec2 previous = LodSelection.Selected[index];
    uint level = 0u;
    if (previous.x != object.State.y) {
        while (level + 1u < object.State.x && distance >= object.Distances[level]) {
            ++level;
        }
    } else {
        level = previous.y;
        while (level > 0u && distance < object.Distances[level - 1u] - hysteresis(object, level - 1u)) {
            --level;
        }
        while (level + 1u < object.State.x && distance >= object.Distances[level] + hysteresis(object, level)) {
            ++level;
        }
    }
    LodSelection.Selected[index] = uvec2(object.State.y, level);
}
