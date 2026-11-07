#define SP_SURFACE_CACHE_SET 0
#define SP_SURFACE_CACHE_BINDING 6
#include "spSurfaceCards.glsl"

#define RT_LIGHTS_BINDING 7
#define RT_LIGHTS_INSTANCE SdfCacheLights
#include "../../raytracing/rtLighting.glsl"

float rtLightVisibility(vec3 position,vec3 normal,vec3 direction,float distance) {
    SpSdfHit blocker;
    return spTraceSdfHit(position,normal,direction,distance,blocker)?0.0:1.0;
}
