#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#include "vgSoftwareCommon.glsl"

layout(std430, set = 0, binding = 9) readonly buffer Winners { uint64_t Keys[]; } winners;
layout(location = 0) out ivec2 ids;

void main() {
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    if (any(greaterThanEqual(pixel, software.Extent.xy))) discard;
    uint64_t key = winners.Keys[pixel.y * software.Extent.x + pixel.x];
    if (key == ~uint64_t(0)) discard;
    uint fields = uint(key);
    ids = ivec2(fields >> 7u, fields & 127u);
    gl_FragDepth = uintBitsToFloat(uint(key >> 32u));
}
