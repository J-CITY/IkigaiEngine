layout(set=1,binding=0) uniform accelerationStructureEXT SceneAS;
struct Triangle {vec4 P0,P1,P2,N0,N1,N2,Albedo,Properties,UV01,UV2;};
layout(std430,set=1,binding=1) readonly buffer Triangles {Triangle triangles[];} RTGeometry;
layout(std430,set=1,binding=2) readonly buffer Instances {uvec4 offsets[];} RTInstances;
layout(set=1,binding=3) uniform sampler2D RTAlbedoMaps[64];
const float SP_RAY_BIAS=0.005;

// Alpha test of a candidate triangle (same rule as visibility.comp and alpha.rahit).
bool spOpaque(uint instance,uint primitive,vec2 hitBary) {
    uvec4 instanceData=RTInstances.offsets[instance];
    // Zero keeps instance-indexed textures for legacy scene providers.
    uint albedoSlot=instanceData.y==0u?uint(instance):instanceData.y-1u;
    Triangle t=RTGeometry.triangles[instanceData.x+primitive];
    vec3 b=vec3(1-hitBary.x-hitBary.y,hitBary);
    vec2 uv=t.UV01.xy*b.x+t.UV01.zw*b.y+t.UV2.xy*b.z;
    return t.Albedo.a*textureLod(RTAlbedoMaps[nonuniformEXT(albedoSlot)],uv,0).a>=.5;
}

float rtLightVisibility(vec3 p,vec3 n,vec3 l,float distance) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query,SceneAS,gl_RayFlagsTerminateOnFirstHitEXT,255,p+n*SP_RAY_BIAS,.001,l,max(distance,.002));
    while(rayQueryProceedEXT(query)) {
        if(rayQueryGetIntersectionTypeEXT(query,false)!=gl_RayQueryCandidateIntersectionTriangleEXT)continue;
        if(spOpaque(rayQueryGetIntersectionInstanceCustomIndexEXT(query,false),rayQueryGetIntersectionPrimitiveIndexEXT(query,false),
               rayQueryGetIntersectionBarycentricsEXT(query,false))) {
            rayQueryConfirmIntersectionEXT(query);
        }
    }
    return rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT?1:0;
}
