#version 460
#extension GL_EXT_ray_tracing : require
struct Data { vec4 Color; uint Step; };
layout(location=0) rayPayloadInEXT Data data;
void main() { data.Color=data.Step==77 ? vec4(.8,.1,.6,1) : vec4(1,0,0,1); }
