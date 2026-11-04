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
float u_Metallic; float u_Roughness; bool u_EnableNormalMapping; int Layer;
} data_UBO;
layout(set=0,binding=10) uniform sampler2D OpaqueDepth;
layout(set=0,binding=11) uniform sampler2D PreviousDepth;
layout(location=0) out vec4 NormalOut;
layout(location=1) out vec4 AlbedoOut; layout(location=2) out vec4 RoughOut;

layout(set=0,binding=7) uniform sampler2D u_MetallicMap;
layout(set=0,binding=13) uniform sampler2D u_RoughnessMap;
layout(set=0,binding=14) uniform sampler2D u_AmbientOcclusionMap;
layout(set=0,binding=15) uniform sampler2D u_HeightMap;
#include "../lib/pbrLayers.glsl"
#include "../lib/surfaceWeather.glsl"
void main() { surfaceCoverage(UV);
vec2 screen=gl_FragCoord.xy/vec2(textureSize(OpaqueDepth,0));
if(gl_FragCoord.z>texture(OpaqueDepth,screen).r+1e-6) discard;
if(data_UBO.Layer>0 && gl_FragCoord.z<=texture(PreviousDepth,screen).r+1e-6) discard;
PbrSurface surface=EvaluatePbrSurface(UV);
if(PbrDiscard(surface.opacity)) discard;
vec3 weatherNormal=normalize(TBN*surface.normal)*(gl_FrontFacing?1:-1);
surfaceWeather(surface.color,surface.roughness,weatherNormal,World);
NormalOut=vec4(weatherNormal,surface.opacity);
AlbedoOut=vec4(surface.color,surface.metallic);
RoughOut=vec4(surface.roughness,surface.ao,-(engine_UBO.View*vec4(World,1)).z,1);
}
