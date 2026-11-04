#version 450
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(set=0,binding=8) uniform sampler2D Opaque;
layout(set=0,binding=9) uniform sampler2D Scene;
layout(location=0) out float Color;
void main() {
    vec3 opaque=texture(Opaque,UV).rgb,scene=texture(Scene,UV).rgb;
    vec3 delta=abs(scene-opaque)/(vec3(0.1)+abs(scene)+abs(opaque));
    Color=clamp(max(delta.r,max(delta.g,delta.b))*2,0,0.9);
}
