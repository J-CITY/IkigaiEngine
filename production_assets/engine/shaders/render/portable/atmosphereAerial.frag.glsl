#version 450
#define IKIGAI_ATMOSPHERE_BUILD_AERIAL
#include "../lib/environment.glsl"
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
void main() {
    vec2 pixel=floor(UV*vec2(768,128));
    float slice=floor(pixel.x/float(AtmosphereAerialAzimuth));
    vec2 coordinates=vec2(mod(pixel.x,float(AtmosphereAerialAzimuth))/float(AtmosphereAerialAzimuth-1),
        mod(pixel.y,float(AtmosphereAerialElevation))/float(AtmosphereAerialElevation-1));
    AtmosphereSegment aerial=atmosphereIntegrateAerial(atmosphereSkyDirection(coordinates),atmosphereAerialDistance(slice));
    Color=vec4(pixel.y<float(AtmosphereAerialElevation)?aerial.Radiance:aerial.Transmittance,1);
}
