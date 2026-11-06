const int SP_SURFACE_TILE=8;

struct SurfaceCard {
    vec4 OriginTolerance;
    vec4 AxisU;
    vec4 AxisV;
    vec4 AlbedoRoughness;
    vec4 Emissive;
};

layout(std140,set=2,binding=8) uniform SurfaceCacheBlock {
    ivec4 State;
    SurfaceCard Cards[16];
} SurfaceCache;
