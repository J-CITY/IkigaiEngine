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
layout(std140,set=0,binding=4) uniform Parameters { float Radius; float Strength; } data_UBO;

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
vec3 p=worldPosition(UV,d),n=normalize(texture(Normals,UV).xyz),sum=vec3(0);
float confidence=0;
for(int i=0;i<16;++i) {
float a=float(i)*2.399963,r=sqrt((float(i)+0.5)/16.0)*data_UBO.Radius;
vec2 uv=UV+vec2(cos(a),sin(a))*r/engine_UBO.ViewportSize;
if(!inside(uv)) continue; float sd=texture(Depth,uv).r; if(sd>=0.999999) continue;
vec3 q=worldPosition(uv,sd),delta=q-p; float distance=length(delta);
if(distance<1e-4) continue;
vec3 direction=delta/distance; vec3 otherNormal=normalize(texture(Normals,uv).xyz);
float weight=max(dot(n,direction),0)*max(dot(otherNormal,-direction),0)/(1+distance*distance);
sum+=texture(Scene,uv).rgb*weight;
confidence+=weight;
} Color=vec4(sum*(data_UBO.Strength/16),clamp(confidence,0,1));
}
