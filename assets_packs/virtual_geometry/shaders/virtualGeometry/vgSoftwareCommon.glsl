// Shared CPU/GLSL packet contract. Keep this in sync with VirtualGeometrySoftwareRaster.
const uint MaxTriangles = 128u;
layout(std140, set = 0, binding = 1) uniform SoftwareParameters {
    uvec4 Draw;   // first global draw, first tail draw relative to batch, tail draw count
    uvec4 Extent; // width, height
} software;

struct ProjectedTriangle {
    vec4 FirstTwo;      // first.xy, second.xy
    vec4 ThirdAndDepth; // third.xy, constant depth, reserved
    ivec4 Bounds;      // inclusive minimum, exclusive maximum
};

float edge(vec2 a, vec2 b, vec2 p) {
    vec2 d = b - a;
    vec2 r = p - a;
    return d.x * r.y - d.y * r.x;
}
