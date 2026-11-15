#version 450
#define IKIGAI_ATMOSPHERE_BUILD_TRANSMITTANCE
#include "../lib/environment.glsl"
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
void main() {
    vec2 coordinates=atmosphereCoordinates(UV,ivec2(256,64));
    float height=max(.01,coordinates.y*coordinates.y*AtmosphereTop);
    Color=vec4(atmosphereTransmittanceDirect(height,coordinates.x*2-1),1);
}
