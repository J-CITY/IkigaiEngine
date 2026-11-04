#include "cloudShadowDirect.glsl"
layout(set=0,binding=46) uniform sampler2D engine_CloudShadowMapTexture;

float environmentCloudShadow(vec3 world,vec3 lightDirection) {
    if(!environmentEnabled() || engine_Environment.CloudLayer.z<=0
        || dot(lightDirection,environmentSun())<.999 || environmentSun().y<=.01) return 1;
    float altitude=length(world-vec3(0,-6000,0))-6000;
    if(engine_Environment.CloudShadowMap.w<.5 || environmentSun().y<.1 || altitude>=engine_Environment.CloudLayer.x)
        return environmentCloudShadowDirect(world,lightDirection);
    vec2 projected=world.xz-environmentSun().xz*world.y/environmentSun().y;
    vec2 uv=(projected-engine_Environment.CloudShadowMap.xy)
        /(engine_Environment.CloudShadowMap.z*2)+.5;
    vec2 border=min(uv,1-uv);
    float edge=smoothstep(0,.08,min(border.x,border.y));
    if(edge<=0) return 1;
    return mix(1,texture(engine_CloudShadowMapTexture,uv).r,edge);
}
