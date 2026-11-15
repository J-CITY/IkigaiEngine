#ifndef IKIGAI_CLOUD_DENSITY_GLSL
#define IKIGAI_CLOUD_DENSITY_GLSL
#include "environment.glsl"
float environmentDensity(vec3 p,sampler3D shapeNoise,sampler3D detailNoise,sampler2D curlNoise) {
    float radius=6000;
    float height=(length(p-vec3(0,-radius,0))-radius-engine_Environment.CloudLayer.x)
        /max(engine_Environment.CloudLayer.y-engine_Environment.CloudLayer.x,.001);
    float coverage=engine_Environment.CloudLayer.z;
    if(height<=0 || height>=1 || coverage<=0) return 0;
    vec3 q=p-vec3(engine_Environment.CloudOffset.x,0,engine_Environment.CloudOffset.y);
    q.xz+=(textureLod(curlNoise,q.xz*.005,0).rg*2-1)*12;
    vec4 shape=textureLod(shapeNoise,q*.003,0);
    float base=clamp((shape.r-(1-coverage))/max(coverage,.001),0,1);
    float detail=dot(textureLod(detailNoise,q*.007,0).rgb,vec3(.625,.25,.125));
    float type=engine_Environment.CloudShape.x;
    float bottom=type<.5?.18:type<1.5?.06:.12;
    float top=type<.5?.55:type<1.5?.85:.8;
    float profile=smoothstep(0,bottom,height)*(1-smoothstep(top,1,height));
    float erosion=type>1.5?.09:type>.5?.08:.18;
    return max(base-detail*erosion,0)*profile*engine_Environment.CloudLayer.w;
}
#endif
