#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
struct Light { vec3 pos; float cutoff; vec3 forward; float outerCutoff;
vec3 color; float constant; int type; float linear; float quadratic; float intensity;
float radius; float padding0; float padding1; float padding2; };
layout(std430,set=0,binding=1) readonly buffer Lights { Light lights[]; } engine_Lights;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(std140,set=0,binding=4) uniform Parameters { float Distance; float Thickness; float Bias; } data_UBO;

bool inside(vec2 uv) { return all(greaterThanEqual(uv,vec2(0))) && all(lessThanEqual(uv,vec2(1))); }
vec3 worldPosition(vec2 uv,float depth) {
uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w);
vec4 h=engine_Frame.InvViewProjection*vec4(uv*2.0-1.0,mix(depth*2.0-1.0,depth,engine_Frame.CameraParams.z),1);
return h.xyz/max(abs(h.w),1e-6)*sign(h.w);
}
vec3 projectWorld(vec3 p) {
vec4 h=engine_UBO.Projection*engine_UBO.View*vec4(p,1);
vec2 uv=h.xy/max(h.w,1e-6)*0.5+0.5;
uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w); return vec3(uv,h.w);
}
float viewDepth(vec3 p) { return -(engine_UBO.View*vec4(p,1)).z; }

void main() {
float d=texture(Depth,UV).r;
if(d>=0.999999) { Color=vec4(1); return; }
vec3 p=worldPosition(UV,d),direction=vec3(0); bool found=false;
for(int i=0;i<engine_Lights.lights.length();++i) if(engine_Lights.lights[i].type==1) { direction=normalize(-engine_Lights.lights[i].forward); found=true; break; }
float visibility=1;
if(found) for(int i=1;i<=24;++i) {
vec3 ray=p+direction*(float(i)/24)*data_UBO.Distance;
vec3 q=projectWorld(ray); if(!inside(q.xy)||q.z<=0) break;
float sd=texture(Depth,q.xy).r; if(sd>=0.999999) continue;
float delta=viewDepth(ray)-viewDepth(worldPosition(q.xy,sd));
if(delta>data_UBO.Bias && delta<data_UBO.Thickness) { visibility=0; break; }
} Color=vec4(vec3(visibility),1);
}
