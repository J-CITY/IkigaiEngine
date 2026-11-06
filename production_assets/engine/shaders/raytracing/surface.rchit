#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "surface.glsl"
layout(location=0) rayPayloadInEXT SurfacePayload hit;
hitAttributeEXT vec2 bary;
struct Triangle {vec4 P0,P1,P2,N0,N1,N2,Albedo,Properties,UV01,UV2;};
layout(std430,set=1,binding=1) readonly buffer Triangles {Triangle triangles[];} RTGeometry;
layout(std430,set=1,binding=2) readonly buffer Instances {uvec4 offsets[];} RTInstances;
layout(set=1,binding=3) uniform sampler2D RTAlbedoMaps[64];
void main() {
    uvec4 instanceData=RTInstances.offsets[gl_InstanceCustomIndexEXT];
    // Zero keeps instance-indexed textures for legacy scene providers.
    uint albedoSlot=instanceData.y==0u?uint(gl_InstanceCustomIndexEXT):instanceData.y-1u;
    Triangle t=RTGeometry.triangles[instanceData.x+gl_PrimitiveID];
    vec3 b=vec3(1-bary.x-bary.y,bary);
    vec3 n=t.N0.xyz*b.x+t.N1.xyz*b.y+t.N2.xyz*b.z;
    if(dot(n,n)<1e-10)n=cross(t.P1.xyz-t.P0.xyz,t.P2.xyz-t.P0.xyz);
    hit.N=normalize(n);if(dot(hit.N,gl_WorldRayDirectionEXT)>0)hit.N=-hit.N;
    hit.P=gl_WorldRayOriginEXT+gl_HitTEXT*gl_WorldRayDirectionEXT;hit.T=gl_HitTEXT;
    vec2 uv=t.UV01.xy*b.x+t.UV01.zw*b.y+t.UV2.xy*b.z;
    hit.Albedo=pow(max(t.Albedo.rgb*textureLod(RTAlbedoMaps[nonuniformEXT(albedoSlot)],uv,0).rgb,vec3(0)),vec3(2.2));
    hit.Roughness=clamp(t.Properties.x,.04,1);hit.Metallic=clamp(t.Properties.y,0,1);
    hit.Emissive=max(vec3(t.Properties.zw,t.UV2.z),vec3(0));
}
