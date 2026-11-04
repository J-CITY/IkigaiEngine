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
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Scene;
layout(set=0,binding=11) uniform sampler2D Roughness;
layout(std140,set=0,binding=4) uniform Parameters { float Step; float Thickness; float Bias; } data_UBO;

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
Color=vec4(0); float d=texture(Depth,UV).r; if(d>=0.999999) return;
vec3 p=worldPosition(UV,d),n=normalize(texture(Normals,UV).xyz);
vec3 direction=reflect(normalize(p-engine_UBO.ViewPos),n);
vec3 ray=p+n*data_UBO.Bias;
for(int i=0;i<64;++i) {
ray+=direction*data_UBO.Step;
vec3 q=projectWorld(ray); if(!inside(q.xy)||q.z<=0) break;
float sd=texture(Depth,q.xy).r; if(sd>=0.999999) continue;
float delta=viewDepth(ray)-viewDepth(worldPosition(q.xy,sd));
if(delta>0 && delta<data_UBO.Thickness) {
float edge=clamp(min(min(q.x,q.y),min(1-q.x,1-q.y))*10,0,1);
float confidence=edge*(1-float(i)/64)*(1-texture(Roughness,UV).r);
Color=vec4(texture(Scene,q.xy).rgb*confidence,confidence); break;
}
}
}
