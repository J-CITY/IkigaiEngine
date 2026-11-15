#version 450
#define IKIGAI_ATMOSPHERE_BUILD_SKYVIEW
#include "../lib/environment.glsl"
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
void main() {
    vec2 coordinates=atmosphereCoordinates(UV,ivec2(192,108));
    Color=vec4(atmosphereIntegrateSky(atmosphereSkyDirection(coordinates)),1);
}
