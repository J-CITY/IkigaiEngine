#version 460
#extension GL_EXT_ray_tracing : require
struct Data { vec4 Color; uint Step; };
layout(location=0) rayPayloadInEXT Data data;
hitAttributeEXT vec2 attributes;
void main() {
    vec3 world=gl_ObjectToWorldEXT*vec4(attributes,0,1);
    vec3 local=gl_WorldToObjectEXT*vec4(world,1);
    data.Color=data.Step==123 ? vec4(.1,.8,.3,1) : vec4(local.xy,float(gl_InstanceCustomIndexEXT)/255.0,1);
    if(gl_PrimitiveID!=0 || gl_GeometryIndexEXT!=0 || gl_HitKindEXT!=0x55u || abs(gl_HitTEXT-1.5)>.001)
        data.Color=vec4(1,0,0,1);
}
