#include "spSdfSurfaceLighting.glsl"
layout(set=0,binding=17) uniform sampler2D SurfaceRadiance;

bool spReadSdfSurfaceCache(vec3 position,vec3 normal,vec3 albedo,vec3 emissive,out vec3 radiance) {
    for(int index=0;index<SurfaceCache.State.x;++index) {
        SurfaceCard card=SurfaceCache.Cards[index];
        if(any(greaterThan(abs(albedo-card.AlbedoRoughness.rgb),vec3(0.003)))
            ||any(greaterThan(abs(emissive-card.Emissive.rgb),vec3(0.01))))continue;
        vec3 delta=position-card.OriginTolerance.xyz;
        vec3 axisU=card.AxisU.xyz,axisV=card.AxisV.xyz;
        vec3 cardNormal=normalize(cross(axisU,axisV));
        if(abs(dot(delta,cardNormal))>card.OriginTolerance.w||dot(normal,cardNormal)<0.999)continue;
        vec2 uv=vec2(dot(delta,axisU)/dot(axisU,axisU),dot(delta,axisV)/dot(axisV,axisV));
        if(any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0))))continue;
        vec2 texel=clamp(uv*SP_SURFACE_TILE,vec2(0.5),vec2(SP_SURFACE_TILE-0.5));
        texel.y+=float(index*SP_SURFACE_TILE);
        vec4 value=textureLod(SurfaceRadiance,texel/vec2(textureSize(SurfaceRadiance,0)),0.0);
        if(value.a!=1.0)continue;
        radiance=value.rgb;
        return true;
    }
    return false;
}

bool spTraceCachedSdf(vec3 position,vec3 normal,vec3 direction,out vec3 radiance,out float kind) {
    SpSdfHit hit;
    if(!spTraceSdfHit(position,normal,direction,engine_Frame.CameraParams.y,hit))return false;
    vec3 albedo,emissive;
    spSdfMaterial(hit,albedo,emissive);
    vec3 gradient=spSdfGradient(hit);
    float gradientSquared=dot(gradient,gradient);
    vec3 hitNormal=gradientSquared>1e-6?normalize(gradient):-direction;
    vec3 worldPosition=hit.Grid.xyz+hit.LocalPosition*hit.Grid.w;
    float distance=spSdfInterpolated(hit.LocalPosition,hit.Size,hit.FirstRow);
    // A march stops within a threshold of the surface. Project that near sample
    // toward the zero crossing before the tighter world-space card test.
    vec3 correction=gradient*(distance/max(gradientSquared,1e-6));
    bool canProject=gradientSquared>1e-6&&dot(correction,correction)<=0.25;
    if(canProject)worldPosition-=correction*hit.Grid.w;
    if(canProject&&spReadSdfSurfaceCache(worldPosition,hitNormal,albedo,emissive,radiance)) {
        kind=SP_KIND_SDF_CACHE;
    } else {
        radiance=rtAccumulateDiffuseLight(emissive,worldPosition,hitNormal,albedo);
        kind=SP_KIND_SDF;
    }
    radiance=clamp(radiance+spSdfAmbient(albedo,hitNormal),vec3(0.0),vec3(65504.0));
    return true;
}
