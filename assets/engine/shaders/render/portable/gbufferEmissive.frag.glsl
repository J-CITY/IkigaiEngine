#version 450
// Opt-in G-buffer variant: gbuffer.frag.glsl plus an Emissive target (location 4).
// Pipelines declare the extra attachment in the Geometry framebuffer and use gbufferEmissive.mat as the
// Geometry override material; pipelines that use gbuffer.mat are unaffected.
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;

layout(location=0) in vec2 UV; layout(location=1) in vec3 World;
layout(location=2) in mat3 TBN; layout(location=5) in vec4 PreviousClip;
layout(set=0,binding=8) uniform sampler2D u_AlbedoMap;
layout(set=0,binding=9) uniform sampler2D u_NormalMap;
layout(std140,set=0,binding=4) uniform Parameters {
vec4 u_Albedo; vec2 u_TextureTiling; vec2 u_TextureOffset;
float u_Metallic; float u_Roughness; bool u_EnableNormalMapping;
vec4 u_EmissiveColor; // rgb: linear colour already scaled by intensity (HDR, may exceed 1), a: unused
} data_UBO;
layout(location=1) out vec4 NormalOut;
layout(location=2) out vec4 AlbedoOut; layout(location=3) out vec4 RoughOut;
layout(location=0) out vec4 VelocityOut;
layout(location=4) out vec4 EmissiveOut;
void main() {
vec2 uv=UV*data_UBO.u_TextureTiling+data_UBO.u_TextureOffset;
vec4 albedo=texture(u_AlbedoMap,uv)*data_UBO.u_Albedo;
if(albedo.a<0.5) discard;
vec3 n=normalize(TBN[2]);
if(data_UBO.u_EnableNormalMapping) n=normalize(TBN*(texture(u_NormalMap,uv).xyz*2-1));
NormalOut=vec4(n,1);
AlbedoOut=vec4(pow(max(albedo.rgb,vec3(0)),vec3(2.2)),clamp(data_UBO.u_Metallic,0,1));
RoughOut=vec4(clamp(data_UBO.u_Roughness,0.04,1),1,-(engine_UBO.View*vec4(World,1)).z,1);
vec2 previous=PreviousClip.xy/max(abs(PreviousClip.w),1e-5)*sign(PreviousClip.w)*0.5+0.5;
previous.y=mix(previous.y,1-previous.y,engine_Frame.CameraParams.w);
VelocityOut=vec4(engine_Frame.FrameState.x>0.5 ? gl_FragCoord.xy/engine_UBO.ViewportSize-previous-engine_Frame.FrameState.zw : vec2(0),0,1);
EmissiveOut=vec4(clamp(data_UBO.u_EmissiveColor.rgb,vec3(0),vec3(65504.0)),1);
}
