#ifndef IKIGAI_SCREEN_PROBE_COMMON_GLSL
#define IKIGAI_SCREEN_PROBE_COMMON_GLSL
// Screen probe gather (hybrid-sp, M3-3): shared declarations of the compute stages.
// Conventions: G-buffer textures share the render size; uv is texture space (CameraParams.w flips Y for VK/DX12);
// depth is standard (0 = near, larger is farther, CameraParams.z selects [0,1] or GL [-1,1] NDC).
layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4 View;
    mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection;
    mat4 PreviousViewProjection;
    mat4 PreviousModel;
    vec4 CameraParams;
    vec4 FrameState;
} engine_Frame;
#include "screenProbeSettings.glsl"
#include "screenProbeMath.glsl"

vec3 spWorldPosition(vec2 uv,float depth) {
    uv.y=mix(uv.y,1.0-uv.y,engine_Frame.CameraParams.w);
    vec4 h=engine_Frame.InvViewProjection*vec4(uv*2.0-1.0,mix(depth*2.0-1.0,depth,engine_Frame.CameraParams.z),1.0);
    return h.xyz/h.w;
}
float spViewDepth(vec3 p) { return -(engine_UBO.View*vec4(p,1.0)).z; }
// Linear view depth of a stored depth value (perspective projection).
float spLinearDepth(float depth) {
    float n=engine_Frame.CameraParams.x,f=engine_Frame.CameraParams.y;
    if(engine_Frame.CameraParams.z>0.5) {
        return n*f/max(f-depth*(f-n),1e-6);
    }
    float z=depth*2.0-1.0;
    return 2.0*n*f/max((f+n)-z*(f-n),1e-6);
}
// Clip-space position -> (texel x, texel y, stored depth).
vec3 spClipToScreen(vec4 h,vec2 size) {
    vec3 ndc=h.xyz/h.w;
    vec2 uv=ndc.xy*0.5+0.5;
    uv.y=mix(uv.y,1.0-uv.y,engine_Frame.CameraParams.w);
    return vec3(uv*size,mix(ndc.z*0.5+0.5,ndc.z,engine_Frame.CameraParams.z));
}
#endif
