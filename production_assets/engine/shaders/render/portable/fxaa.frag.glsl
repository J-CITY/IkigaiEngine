#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Scene;

float luma(vec3 c) { return dot(c,vec3(0.299,0.587,0.114)); }
void main() { vec2 px=1.0/vec2(textureSize(Scene,0)); vec3 c=texture(Scene,UV).rgb;
float n=luma(texture(Scene,UV+vec2(0,px.y)).rgb),s=luma(texture(Scene,UV-vec2(0,px.y)).rgb);
float e=luma(texture(Scene,UV+vec2(px.x,0)).rgb),w=luma(texture(Scene,UV-vec2(px.x,0)).rgb),m=luma(c);
float range=max(max(n,s),max(e,w))-min(min(n,s),min(e,w));
if(range<max(0.0312,m*0.125)) {Color=vec4(c,1);return;}
vec2 dir=normalize(vec2(-(n-s),e-w)+vec2(1e-6))*px;
Color=vec4((texture(Scene,UV-dir*0.5).rgb+texture(Scene,UV+dir*0.5).rgb)*0.5,1); }
