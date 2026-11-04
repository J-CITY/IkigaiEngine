#ifndef IKIGAI_DEFORMATION_GLSL
#define IKIGAI_DEFORMATION_GLSL
#include "environment.glsl"
layout(std140,set=0,binding=41) uniform SurfaceState {
    vec4 Parameters; // mode, stiffness, max bend, wind strength
    vec4 Shape; // height, root height, flutter, weather response
    vec4 Fur; // length, shells, density, procedural coverage
    vec4 Pivot;
} engine_Surface;
layout(std140,set=0,binding=42) uniform InteractionState {
    vec4 Count;
    vec4 StartRadius[32];
    vec4 EndStrength[32];
    vec4 PreviousStartRadius[32];
    vec4 PreviousEndStrength[32];
    vec4 Owners[32];
} engine_Interactions;

vec3 deformationSafeUnit(vec3 v,vec3 fallback) { return dot(v,v)>1e-9 ? normalize(v) : fallback; }
float furShell() { return engine_Surface.Parameters.x>2.5 ? float(gl_InstanceIndex%max(int(engine_Surface.Fur.y),2))/max(engine_Surface.Fur.y-1,1) : 0; }
vec3 deformLocal(vec3 local,vec3 normal) {
    return local+normal*(furShell()*engine_Surface.Fur.x);
}
vec3 deformWorld(vec3 world,vec3 root,float weight,bool previous) {
    if(engine_Surface.Parameters.x<.5) return world;
    float time=engine_Environment.Timing.x-(previous?engine_Environment.Timing.y:0);
    float phase=dot(root.xz,vec2(.13,.17));
    float gust=1+engine_Environment.Wind.z*sin(time*1.7+phase);
    vec3 wind=vec3(engine_Environment.Wind.x,0,engine_Environment.Wind.y);
    vec3 bend=wind*engine_Surface.Parameters.w*gust*(.45+.55*sin(time+phase));
    if(engine_Surface.Parameters.x>1.5) bend+=vec3(sin(time*7+phase),.2*cos(time*9+phase),cos(time*6+phase))*engine_Surface.Shape.z;
    for(int i=0;i<min(int(engine_Interactions.Count.x),32);++i) {
        if(engine_Interactions.Owners[i].x==engine_Surface.Pivot.w) continue;
        vec4 a=previous?engine_Interactions.PreviousStartRadius[i]:engine_Interactions.StartRadius[i];
        vec4 b=previous?engine_Interactions.PreviousEndStrength[i]:engine_Interactions.EndStrength[i];
        vec3 segment=b.xyz-a.xyz;
        float along=clamp(dot(world-a.xyz,segment)/max(dot(segment,segment),1e-6),0,1);
        vec3 delta=world-(a.xyz+segment*along);
        float distance=length(delta);
        float influence=1-smoothstep(0,max(a.w,.001),distance);
        bend+=deformationSafeUnit(delta,vec3(1,0,0))*influence*b.w;
    }
    float flexibility=1-engine_Surface.Parameters.y;
    float maximum=engine_Surface.Parameters.z;
    bend*=flexibility*weight*weight;
    float magnitude=length(bend);
    if(magnitude>maximum) bend*=maximum/max(magnitude,1e-6);
    vec3 offset=world-root;
    float radius=length(offset);
    if(radius<1e-5) return world;
    // Rotate the root-to-tip direction and preserve its length.
    return root+deformationSafeUnit(offset+bend,offset/radius)*radius;
}
float deformationWeight(vec3 local) {
    return engine_Surface.Parameters.x>2.5 ? furShell() : clamp((local.y-engine_Surface.Shape.y)/max(engine_Surface.Shape.x,.001),0,1);
}
#endif
