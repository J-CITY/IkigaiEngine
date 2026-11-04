#ifndef IKIGAI_CLOUD_SHADOW_DIRECT_GLSL
#define IKIGAI_CLOUD_SHADOW_DIRECT_GLSL
#include "environmentCloudDensity.glsl"
#ifdef IKIGAI_CLOUD_SHADOW_SHARED_NOISE
#define IKIGAI_SHADOW_SHAPE ShapeNoise
#define IKIGAI_SHADOW_DETAIL DetailNoise
#define IKIGAI_SHADOW_CURL CurlNoise
#else
layout(set=0,binding=43) uniform sampler3D engine_CloudShape;
layout(set=0,binding=44) uniform sampler3D engine_CloudDetail;
layout(set=0,binding=45) uniform sampler2D engine_CloudCurl;
#define IKIGAI_SHADOW_SHAPE engine_CloudShape
#define IKIGAI_SHADOW_DETAIL engine_CloudDetail
#define IKIGAI_SHADOW_CURL engine_CloudCurl
#endif

float environmentCloudShadowDirect(vec3 world,vec3 lightDirection) {
    if(!environmentEnabled() || engine_Environment.CloudLayer.z<=0
        || dot(lightDirection,environmentSun())<.999 || environmentSun().y<=.01) return 1;
    vec3 sun=environmentSun();
    vec3 relative=world-vec3(0,-6000,0);
    float b=dot(relative,sun);
    float innerRadius=6000+engine_Environment.CloudLayer.x;
    float outerRadius=6000+engine_Environment.CloudLayer.y;
    float innerDisc=b*b-dot(relative,relative)+innerRadius*innerRadius;
    float outerDisc=b*b-dot(relative,relative)+outerRadius*outerRadius;
    if(outerDisc<=0) return 1;
    float start=innerDisc>0?max(-b+sqrt(innerDisc),0):0;
    if(length(relative)>innerRadius) start=0;
    float finish=max(-b+sqrt(outerDisc),start);
    int count=environmentCloudShadowSteps();
    float stepLength=(finish-start)/float(count),depth=0;
    for(int i=0;i<8;++i) {
        if(i>=count) break;
        vec3 p=world+sun*(start+(float(i)+.5)*stepLength);
        depth+=environmentDensity(p,IKIGAI_SHADOW_SHAPE,IKIGAI_SHADOW_DETAIL,IKIGAI_SHADOW_CURL)*stepLength;
    }
    return mix(1,exp(-depth),engine_Environment.CloudShape.z);
}
#endif
