#include "../indirectLighting/screenProbes/spSurfaceCards.glsl"
#include "glossySurfaceDirections.glsl"

layout(set=2,binding=9) uniform sampler2D SurfaceRadiance;

vec4 glossyReadSurfaceTile(vec2 localTexel,vec2 atlasSize,int card,int azimuth,int elevation) {
    vec2 texel=vec2(glossySurfaceTile(card,azimuth,elevation))+localTexel;
    return textureLod(SurfaceRadiance,texel/atlasSize,0.0);
}

bool glossyReadSurfaceCache(SurfacePayload surface,vec3 view,out vec3 radiance) {
    if(surface.Metallic>0.01||surface.Roughness<0.7||dot(surface.N,view)<GlossyMinViewCosine)return false;
    for(int i=0;i<SurfaceCache.State.x;++i) {
        SurfaceCard card=SurfaceCache.Cards[i];
        if(abs(surface.Roughness-card.AlbedoRoughness.w)>0.01
            ||any(greaterThan(abs(surface.Albedo-card.AlbedoRoughness.rgb),vec3(0.003)))
            ||any(greaterThan(abs(surface.Emissive-card.Emissive.rgb),vec3(0.01))))continue;

        vec3 u=card.AxisU.xyz,v=card.AxisV.xyz;
        vec3 n=normalize(cross(u,v));
        vec3 delta=surface.P-card.OriginTolerance.xyz;
        float viewCosine=dot(n,view);
        if(abs(dot(delta,n))>card.OriginTolerance.w||dot(n,surface.N)<0.999||viewCosine<GlossyMinViewCosine)continue;
        vec2 uv=vec2(dot(delta,u)/dot(u,u),dot(delta,v)/dot(v,v));
        if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))continue;

        vec2 planar=vec2(dot(view,normalize(u)),dot(view,normalize(v)));
        float azimuth=dot(planar,planar)>1e-8?atan(planar.y,planar.x)/GlossyAzimuthStep:0.0;
        azimuth=mod(azimuth,float(GlossyAzimuthCount));
        float elevation=clamp((viewCosine-GlossyMinViewCosine)/GlossyElevationStep,0.0,float(GlossyElevationCount-1));
        int a=int(floor(azimuth))%GlossyAzimuthCount;
        int nextA=(a+1)%GlossyAzimuthCount;
        int e=int(floor(elevation));
        int nextE=min(e+1,GlossyElevationCount-1);
        // Spatial filtering stays within a tile; angular filtering wraps only in azimuth.
        vec2 localTexel=clamp(uv*SP_SURFACE_TILE,vec2(0.5),vec2(SP_SURFACE_TILE-0.5));
        vec2 atlasSize=vec2(textureSize(SurfaceRadiance,0));
        vec4 low=mix(glossyReadSurfaceTile(localTexel,atlasSize,i,a,e),
                     glossyReadSurfaceTile(localTexel,atlasSize,i,nextA,e),fract(azimuth));
        vec4 high=mix(glossyReadSurfaceTile(localTexel,atlasSize,i,a,nextE),
                      glossyReadSurfaceTile(localTexel,atlasSize,i,nextA,nextE),fract(azimuth));
        vec4 value=mix(low,high,fract(elevation));
        if(value.a!=1.0)continue;
        radiance=value.rgb;
        return true;
    }
    return false;
}
