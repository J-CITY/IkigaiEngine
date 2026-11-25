#version 450
#extension GL_GOOGLE_include_directive : require
#include "waves.glsl"

layout(location=0) in vec3 Position;
layout(location=1) in vec2 Texcoord;
layout(push_constant) uniform Model { mat4 model; } engine_Model;
layout(location=0) out vec3 World;
layout(location=1) out vec4 PreviousClip;

// The ocean mesh stores its largest adjacent cell interval in U, in model space.
// Fade displacement as a wavelength falls below four cells. The material can
// still resolve that scale as a normal where it covers several screen pixels.
float geometricWave(vec4 wave, vec2 point, float phase, mat4 model) {
    float phasePerCell = Texcoord.x * length(vec2(dot(wave.xy, model[0].xz), dot(wave.xy, model[2].xz)));
    float weight = 1.0 - smoothstep(0.8, 2.6, phasePerCell);
    return wave.z * weight * sin(dot(wave.xy, point) + phase);
}

vec3 oceanPosition(mat4 model, vec3 phases) {
    vec3 point = (model * vec4(Position, 1.0)).xyz;
    point.y += geometricWave(water_.WaveA, point.xz, phases.x, model)
             + geometricWave(water_.WaveB, point.xz, phases.y, model)
             + geometricWave(water_.WaveC, point.xz, phases.z, model);
    return point;
}

void main() {
    World = oceanPosition(engine_Model.model, vec3(water_.WaveA.w, water_.WaveB.w, water_.WaveC.w));
    vec3 previous = oceanPosition(engine_Frame.PreviousModel, water_.PreviousPhases.xyz);
    PreviousClip = engine_Frame.PreviousViewProjection * vec4(previous, 1.0);
    gl_Position = engine_UBO.Projection * engine_UBO.View * vec4(World, 1.0);
}
