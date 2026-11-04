#version 450
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;

layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=0) out vec2 UV;
void main() { UV=aUV; UV.y=mix(UV.y,1-UV.y,engine_Frame.CameraParams.w); gl_Position=vec4(aPos,1); }
