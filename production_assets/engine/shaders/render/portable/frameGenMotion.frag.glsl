#version 450
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(set=0,binding=8) uniform sampler2D Motion;
layout(std140,set=0,binding=4) uniform FGParameters {vec2 JitterDelta;} data_UBO;
layout(location=0) out vec2 Color;
void main(){Color=engine_Frame.FrameState.x<0.5?vec2(0):texture(Motion,UV).xy-data_UBO.JitterDelta;}
