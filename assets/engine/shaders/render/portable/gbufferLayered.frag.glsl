#version 450
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
float u_Metallic; float u_Roughness; bool u_EnableNormalMapping; float u_Transmission;
} data_UBO;
layout(location=1) out vec4 NormalOut;
layout(location=2) out vec4 AlbedoOut; layout(location=3) out vec4 RoughOut;
layout(location=0) out vec4 VelocityOut;
layout(set=0,binding=7) uniform sampler2D u_MetallicMap;
layout(set=0,binding=13) uniform sampler2D u_RoughnessMap;
layout(set=0,binding=14) uniform sampler2D u_AmbientOcclusionMap;
layout(set=0,binding=15) uniform sampler2D u_HeightMap;
#include "../lib/pbrLayers.glsl"
#include "../lib/surfaceWeather.glsl"
void main() { surfaceCoverage(UV);
PbrSurface surface=EvaluatePbrSurface(UV);
if(PbrDiscard(surface.opacity)) discard;
vec3 n=normalize(TBN*surface.normal);
if(!gl_FrontFacing) n=-n;
surfaceWeather(surface.color,surface.roughness,n,World);
NormalOut=vec4(n,1);
AlbedoOut=vec4(surface.color,surface.metallic);
// Alpha preserves the existing opaque value 1; thin-surface transmission uses 1-alpha.
RoughOut=vec4(surface.roughness,surface.ao,-(engine_UBO.View*vec4(World,1)).z,1-clamp(data_UBO.u_Transmission,0,1));
vec2 previous=PreviousClip.xy/max(abs(PreviousClip.w),1e-5)*sign(PreviousClip.w)*0.5+0.5;
previous.y=mix(previous.y,1-previous.y,engine_Frame.CameraParams.w);
VelocityOut=vec4(engine_Frame.FrameState.x>0.5 ? gl_FragCoord.xy/engine_UBO.ViewportSize-previous-engine_Frame.FrameState.zw : vec2(0),0,1);
}
