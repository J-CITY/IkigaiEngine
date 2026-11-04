#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#include "surface.glsl"
layout(location=0) rayPayloadInEXT SurfacePayload hit;
void main(){hit.T=-1;}
