#version 450
#include "../lib/cloudShadowDirect.glsl"
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
void main() {
    vec2 world=engine_Environment.CloudShadowMap.xy
        +(UV-.5)*2*engine_Environment.CloudShadowMap.z;
    float transmission=environmentCloudShadowDirect(vec3(world.x,0,world.y),environmentSun());
    Color=vec4(transmission,transmission,transmission,1);
}
