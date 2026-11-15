#version 450
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(location=0) in vec2 UV;
layout(set=0,binding=8) uniform sampler2D Velocity;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(location=0) out vec2 Color;
void main() {
    if(engine_Frame.FrameState.x<0.5) {Color=vec2(0);return;}
    // The standard finite depth buffer clears to 1. Preserve even very distant geometry motion.
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    if(texelFetch(Depth,pixel,0).r<1.0) {Color=texelFetch(Velocity,pixel,0).xy;return;}
    vec2 ndc=UV*2-1;ndc.y=mix(ndc.y,-ndc.y,engine_Frame.CameraParams.w);
    vec4 world=engine_Frame.InvViewProjection*vec4(ndc,1,1);
    // Environment is at infinity: camera translation must not move the sky.
    world.xyz-=engine_UBO.ViewPos*world.w;world.w=0;
    vec4 previous=engine_Frame.PreviousViewProjection*world;
    vec2 previousUV=previous.xy/max(abs(previous.w),1e-6)*sign(previous.w)*0.5+0.5;
    previousUV.y=mix(previousUV.y,1-previousUV.y,engine_Frame.CameraParams.w);
    Color=UV-previousUV-engine_Frame.FrameState.zw;
}
