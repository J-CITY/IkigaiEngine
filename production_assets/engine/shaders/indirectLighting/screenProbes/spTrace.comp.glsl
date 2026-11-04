#version 450
// Screen probe trace: one invocation per (probe, direction cell) of the radiance atlas (DirectionSide^2 cells per probe).
// Directions are stratified cosine-weighted about the probe normal (spProbeDirection). Each ray is marched in screen space
// against the closest-depth pyramid (hierarchical skip, thickness test at mip 0). Hit: radiance of the lit scene of this
// frame (blended with the previous HDR frame for multi-bounce while the history is valid). Miss / off-screen / out of
// steps / SCREEN link disabled: environment cube radiance (zero without the ENVIRONMENT link), flagged as a miss.
// Output: rgb radiance, a = SP_KIND_HIT, SP_KIND_MISS or 0 (invalid probe).
#include "screenProbeCommon.glsl"
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D ProbeInfo;
layout(set=0,binding=11) uniform sampler2D HZB;
layout(set=0,binding=12) uniform sampler2D Scene;
layout(set=0,binding=13) uniform sampler2D PreviousScene;
layout(set=0,binding=14) uniform samplerCube Environment;
layout(set=0,binding=15,rgba16f) uniform writeonly image2D Radiance;

struct ScreenHit {
    vec2 Uv;
    float Depth;
};

// Hierarchical march of origin + dir * s through the closest-depth pyramid. Screen coordinates are mip-0 texels.
bool traceScreen(vec3 origin,vec3 normal,vec3 dir,vec2 size,out ScreenHit hit) {
    hit.Uv=vec2(0.0);
    hit.Depth=1.0;
    float nearPlane=engine_Frame.CameraParams.x;
    float viewDepth=spViewDepth(origin);
    float towardViewer=-(engine_UBO.View*vec4(dir,0.0)).z;
    float rayLength=engine_Frame.CameraParams.y;
    if(towardViewer<0.0) {
        rayLength=min(rayLength,(viewDepth-nearPlane*1.5)/-towardViewer);
    }
    if(rayLength<=0.0)return false;
    mat4 viewProjection=engine_UBO.Projection*engine_UBO.View;
    vec3 s0=spClipToScreen(viewProjection*vec4(origin,1.0),size);
    vec3 s1=spClipToScreen(viewProjection*vec4(origin+dir*rayLength,1.0),size);
    vec3 delta=s1-s0;
    // Clip the segment to the viewport.
    float tMax=1.0;
    for(int axis=0;axis<2;++axis) {
        if(delta[axis]>1e-6)tMax=min(tMax,(size[axis]-0.001-s0[axis])/delta[axis]);
        else if(delta[axis]<-1e-6)tMax=min(tMax,(0.001-s0[axis])/delta[axis]);
    }
    float pixels=length(delta.xy);
    if(pixels*tMax<1.0)return false; // Ray (almost) along the view direction: not traceable in screen space.
    float texelStep=1.0/pixels;
    vec2 safeDelta=vec2(abs(delta.x)<1e-6?1e-6:delta.x,abs(delta.y)<1e-6?1e-6:delta.y);
    int maxLevel=clamp(ScreenProbe.Filter.w,0,textureQueryLevels(HZB)-1);
    int level=0;
    float t=1.5*texelStep;
    float thickness=ScreenProbe.Trace.x;
    float planeBias=max(0.002,0.002*viewDepth);
    for(int i=0;i<ScreenProbe.Layout.z;++i) {
        if(t>=tMax)return false;
        vec3 s=s0+delta*t;
        float cellSize=exp2(float(level));
        vec2 cell=floor(s.xy/cellSize);
        vec2 boundary=(cell+step(vec2(0.0),delta.xy))*cellSize;
        vec2 crossing=(boundary-s0.xy)/safeDelta;
        float tExit=min(min(crossing.x,crossing.y),tMax);
        ivec2 texel=min(ivec2(cell),textureSize(HZB,level)-1);
        float closest=texelFetch(HZB,texel,level).r;
        float zEntry=s.z;
        float zExit=s0.z+delta.z*tExit;
        if(max(zEntry,zExit)<closest) {
            // The whole segment inside this cell is in front of every surface below it.
            t=tExit+0.05*texelStep;
            level=min(level+1,maxLevel);
            continue;
        }
        if(level>0) {
            if(delta.z>0.0&&zEntry<closest)t=max(t,(closest-s0.z)/delta.z);
            --level;
            continue;
        }
        // Mip 0: closest is the surface depth of this texel.
        float tHit=(delta.z>0.0&&zEntry<closest)?(closest-s0.z)/delta.z:t;
        float rayDepth=s0.z+delta.z*tHit;
        bool inside=spLinearDepth(rayDepth)-spLinearDepth(closest)<=thickness;
        if(inside&&closest<SP_SKY_DEPTH) {
            vec2 uv=(vec2(texel)+0.5)/size;
            vec3 surface=spWorldPosition(uv,closest);
            vec3 surfaceNormal=normalize(texelFetch(Normals,texel,0).xyz);
            // Reject the probe's own plane (grazing rays) and back faces (ray passing behind a visible surface).
            if(dot(normal,surface-origin)>planeBias&&dot(surfaceNormal,dir)<0.0) {
                hit.Uv=uv;
                hit.Depth=closest;
                return true;
            }
        }
        t=tExit+0.05*texelStep;
    }
    return false;
}

void main() {
    ivec2 texel=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(texel,imageSize(Radiance))))return;
    int side=spDirectionSide();
    ivec2 probe=texel/side;
    ivec2 cell=texel-probe*side;
    ivec2 pixel;
    if(any(greaterThanEqual(probe,textureSize(ProbeInfo,0)))||!spProbePixel(ProbeInfo,probe,pixel)) {
        imageStore(Radiance,texel,vec4(0.0));
        return;
    }
    vec2 size=vec2(textureSize(Depth,0));
    vec2 uv=(vec2(pixel)+0.5)/size;
    float depth=texelFetch(Depth,pixel,0).r;
    vec3 position=spWorldPosition(uv,depth);
    vec3 normal=normalize(texelFetch(Normals,pixel,0).xyz);
    vec3 dir=spProbeDirection(probe,cell,normal,uint(engine_UBO.FrameCount));
    vec3 origin=position+normal*max(0.002,0.001*spViewDepth(position));
    ScreenHit hit;
    if(spChainEnabled(SP_CHAIN_SCREEN)&&traceScreen(origin,normal,dir,size,hit)) {
        vec3 radiance=textureLod(Scene,hit.Uv,0.0).rgb;
        // Multi-bounce: blend the previous final HDR frame (PreviousHdr: direct + emissive + last frame's GI) at the reprojected
        // hit, only while the temporal history of the frame is valid (FrameState.x, HistoryValidity of M3-1).
        float previousWeight=engine_Frame.FrameState.x>0.5?clamp(ScreenProbe.Trace.w,0.0,1.0):0.0;
        if(previousWeight>0.0) {
            vec4 previous=engine_Frame.PreviousViewProjection*vec4(spWorldPosition(hit.Uv,hit.Depth),1.0);
            vec2 previousUv=spClipToScreen(previous,vec2(1.0)).xy;
            if(previous.w>0.0&&all(greaterThanEqual(previousUv,vec2(0.0)))&&all(lessThanEqual(previousUv,vec2(1.0)))) {
                radiance=mix(radiance,textureLod(PreviousScene,previousUv,0.0).rgb,previousWeight);
            }
        }
        imageStore(Radiance,texel,vec4(max(radiance,vec3(0.0)),SP_KIND_HIT));
    }
    else {
        // Miss: environment, or zero when the ENVIRONMENT link is disabled. SPRayQueryTrace (hybrid-sp-rt) continues the chain.
        vec3 environment=spChainEnabled(SP_CHAIN_ENVIRONMENT)?max(textureLod(Environment,dir,0.0).rgb,vec3(0.0)):vec3(0.0);
        imageStore(Radiance,texel,vec4(environment,SP_KIND_MISS));
    }
}
