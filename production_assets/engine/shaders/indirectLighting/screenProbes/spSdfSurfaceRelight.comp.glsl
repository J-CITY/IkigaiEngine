#version 450
#include "screenProbeSettings.glsl"
#include "screenProbeMath.glsl"
#define SP_SDF_GEOMETRY_ONLY
#include "spSdfMarch.glsl"
#include "spSdfSurfaceLighting.glsl"

layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=17,rgba16f) uniform writeonly image2D SurfaceRadiance;
shared uint lightVisibility[SP_SURFACE_TILE*SP_SURFACE_TILE];

uint visibilityAt(ivec2 local) {
    ivec2 texel=clamp(local,ivec2(0),ivec2(SP_SURFACE_TILE-1));
    return lightVisibility[texel.x+texel.y*SP_SURFACE_TILE];
}

void main() {
    ivec2 texel=ivec2(gl_GlobalInvocationID.xy);
    bool inBounds=all(lessThan(texel,imageSize(SurfaceRadiance)));
    int index=int(gl_WorkGroupID.y);
    if(index>=SurfaceCache.State.x||!spChainEnabled(SP_CHAIN_SDF_ACTIVE)) {
        if(inBounds)imageStore(SurfaceRadiance,texel,vec4(0.0));
        return;
    }
    SurfaceCard card=SurfaceCache.Cards[index];
    ivec2 local=ivec2(gl_LocalInvocationID.xy);
    vec2 uv=(vec2(local)+0.5)/SP_SURFACE_TILE;
    vec3 position=card.OriginTolerance.xyz+card.AxisU.xyz*uv.x+card.AxisV.xyz*uv.y;
    vec3 normal=normalize(cross(card.AxisU.xyz,card.AxisV.xyz));
    uint visibleShadowLights;
    vec3 radiance=rtAccumulateDiffuseLight(card.Emissive.rgb,position,normal,card.AlbedoRoughness.rgb,visibleShadowLights);
    lightVisibility[gl_LocalInvocationIndex]=visibleShadowLights;
    barrier();
    bool continuous=visibleShadowLights==visibilityAt(local+ivec2(1,0))
        &&visibleShadowLights==visibilityAt(local+ivec2(-1,0))
        &&visibleShadowLights==visibilityAt(local+ivec2(0,1))
        &&visibleShadowLights==visibilityAt(local+ivec2(0,-1));
    // An interpolated footprint touching a shadow edge uses per-hit lighting.
    if(inBounds)imageStore(SurfaceRadiance,texel,vec4(clamp(radiance,vec3(0.0),vec3(65504.0)),continuous?1.0:0.0));
}
