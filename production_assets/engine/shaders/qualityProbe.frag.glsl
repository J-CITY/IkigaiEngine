#version 450
#define IKIGAI_ATMOSPHERE_BUILD_TRANSMITTANCE
#include "render/lib/environment.glsl"
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=42) uniform InteractionState {
vec4 Count; vec4 StartRadius[32]; vec4 EndStrength[32];
vec4 PreviousStartRadius[32]; vec4 PreviousEndStrength[32]; vec4 Owners[32];
} engine_Interactions;
layout(location=0) out vec4 Color;
void main() {
int column=int(gl_FragCoord.x);
if(column==0) Color=engine_Environment.CloudOffset;
else if(column==1) Color=engine_Environment.PreviousCloudOffset;
else if(column==2) Color=engine_Interactions.Count;
else if(column==3) Color=engine_Frame.FrameState;
else if(column==4) Color=vec4(environmentCloudViewSteps(96),environmentCloudLightSteps(),environmentCloudShadowSteps(),engine_UBO.FrameCount);
else if(column==5) Color=engine_Environment.CloudShadowMap;
else if(column==6) Color=engine_Interactions.StartRadius[0];
else Color=engine_Environment.WeatherState;
}
