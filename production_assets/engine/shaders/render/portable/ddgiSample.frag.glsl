#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../raytracing/ddgiSample.glsl"
layout(location=0) in vec2 UV;layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Irradiance;
layout(set=0,binding=11) uniform sampler2D Distance;
layout(std140,set=0,binding=3) uniform FrameData {mat4 InvViewProjection;mat4 PreviousViewProjection;mat4 PreviousModel;vec4 CameraParams;vec4 FrameState;} engine_Frame;
layout(std140,set=0,binding=4) uniform Parameters {vec4 OriginSpacing;vec4 Counts;} data_UBO;
void main() {
    float d=texture(Depth,UV).r;if(d>=.999999){Color=vec4(0);return;}
    vec2 uv=UV;uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w);
    vec4 p=engine_Frame.InvViewProjection*vec4(uv*2-1,mix(d*2-1,d,engine_Frame.CameraParams.z),1);
    float confidence;vec3 irradiance=probeIrradiance(p.xyz/p.w,normalize(texture(Normals,UV).xyz),data_UBO.OriginSpacing,ivec3(data_UBO.Counts.xyz),Irradiance,Distance,confidence);
    Color=vec4(max(irradiance,vec3(0)),confidence);
}
