#version 450
layout(location=0) in vec2 UV;
layout(location=1) in vec3 World;
layout(location=2) in mat3 TBN;
layout(location=5) in vec4 PreviousClip;
layout(std140,set=0,binding=5) uniform PointShadowPass {
mat4 ViewProjection; vec4 LightPositionFar; vec4 ClipRange;
} engine_PointShadowPass;

layout(set=0,binding=8) uniform sampler2D u_AlbedoMap;
layout(std140,set=0,binding=4) uniform Parameters { vec4 u_Albedo; vec2 u_TextureTiling; vec2 u_TextureOffset; float Near; float Far; } data_UBO;
layout(location=0) out vec4 Color;
layout(set=0,binding=15) uniform sampler2D u_HeightMap;
#define IKIGAI_PBR_COVERAGE_ONLY
#include "../lib/pbrLayers.glsl"
#include "../lib/surfaceWeather.glsl"
void main() { surfaceCoverage(UV); if(PbrDiscard(EvaluatePbrCoverage(UV))) discard; float d=length(World-engine_PointShadowPass.LightPositionFar.xyz);
if(d<engine_PointShadowPass.ClipRange.x || d>engine_PointShadowPass.LightPositionFar.w) discard;
Color=vec4(d/engine_PointShadowPass.LightPositionFar.w,0,0,1); }
