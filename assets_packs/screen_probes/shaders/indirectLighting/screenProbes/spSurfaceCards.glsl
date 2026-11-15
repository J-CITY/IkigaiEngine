const int SP_SURFACE_TILE=8;

struct SurfaceCard {
    vec4 OriginTolerance;
    vec4 AxisU;
    vec4 AxisV;
    vec4 AlbedoRoughness;
    vec4 Emissive;
};

#ifndef SP_SURFACE_CACHE_SET
#define SP_SURFACE_CACHE_SET 2
#endif
#ifndef SP_SURFACE_CACHE_BINDING
#define SP_SURFACE_CACHE_BINDING 8
#endif
layout(std140,set=SP_SURFACE_CACHE_SET,binding=SP_SURFACE_CACHE_BINDING) uniform SurfaceCacheBlock {
    ivec4 State;
    SurfaceCard Cards[16];
} SurfaceCache;
