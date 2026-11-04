#version 450
#include "render/lib/environmentCloudLighting.glsl"
struct Light { vec3 pos; float cutoff; vec3 forward; float outerCutoff;
vec3 color; float constant; int type; float linear; float quadratic; float intensity;
float radius; float padding0; float padding1; float padding2; };
layout(std430,set=0,binding=1) readonly buffer Lights { Light lights[]; } engine_Lights;
layout(location=0) out vec4 Color;
void main() {
    float distance=4*gl_FragCoord.x*gl_FragCoord.x;
    vec2 horizontal=environmentSun().xz;
    horizontal=length(horizontal)>1e-5?normalize(horizontal):vec2(1,0);
    vec3 ray=vec3(horizontal.x*cos(.1),sin(.1),horizontal.y*cos(.1));
    vec3 camera=vec3(0,engine_Environment.AtmosphereLut.z,0);
    int row=int(gl_FragCoord.y);
    if(row==0) Color=vec4(environmentFog(vec3(.2,.3,.4),camera+ray*distance,camera),1);
    else if(row==1) Color=vec4(environmentCloudAmbient(),1);
    else if(row==2) Color=vec4(environmentCloudSun(vec3(0,80,0),environmentSunRadiance()),1);
    else if(row==3) Color=vec4(environmentCloudAerial(vec3(.2,.3,.4),.2,ray,distance),1);
    else {
        int column=int(gl_FragCoord.x),sun=int(engine_Environment.Wind.w)-1;
        float heights[8]=float[8](.01,1,20,80,250,399,410,500);
        vec3 position=vec3(column>=8?200:0,heights[column%8],column>=8?-100:0);
        vec3 radiance=vec3(.7,.5,.3);
        if(row==4) Color=vec4(environmentSurfaceLight(position,1,sun,radiance),1);
        else if(row==5) Color=vec4(environmentSurfaceLight(position,1,sun==0?1:0,radiance),1);
        else if(row==6) Color=vec4(environmentSurfaceLight(position,0,sun,radiance),1);
        else if(row==7) Color=vec4(environmentSurfaceLight(position,2,sun,radiance),1);
        else if(sun>=0 && sun<engine_Lights.lights.length()) {
            Light light=engine_Lights.lights[sun];
            Color=vec4(environmentSurfaceLight(position,light.type,sun,light.color*light.intensity),sun+1);
        } else Color=vec4(0);
    }
}
