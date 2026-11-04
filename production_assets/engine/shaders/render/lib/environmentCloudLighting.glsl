#ifndef IKIGAI_ENVIRONMENT_CLOUD_LIGHTING
#define IKIGAI_ENVIRONMENT_CLOUD_LIGHTING
#include "environment.glsl"
vec3 environmentCloudAmbient() {
    if(environmentEnabled() && engine_Environment.AtmosphereLut.y>.5)
        return max(atmosphereSkyScattering(vec3(0,1,0))*.4,vec3(.0002,.0004,.001));
    return environmentEnabled()?mix(vec3(.001,.002,.006),vec3(.36,.48,.68),engine_Environment.CloudShape.w):vec3(.36,.48,.68);
}
vec3 environmentCloudSun(vec3 position,vec3 fallback) {
    return fallback*environmentSolarTransmittance(position);
}
vec3 environmentCloudAerial(vec3 radiance,float transmittance,vec3 ray,float distance) {
    if(!environmentEnabled() || engine_Environment.AtmosphereLut.w<.5 || distance<=0) return radiance;
    AtmosphereSegment aerial=atmosphereAerialPerspective(ray,distance);
    // The background already contains atmospheric in-scattering. Replace only
    // the fraction occluded by the cloud, preserving the foreground gas once.
    return radiance*aerial.Transmittance+aerial.Radiance*(1-transmittance);
}
#endif
