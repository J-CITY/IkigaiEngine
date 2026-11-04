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
layout(std140,set=0,binding=4) uniform Parameters { float Radius; float Bias; int Samples; } data_UBO;

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
float d=texture(Depth,UV).r; if(d>=0.999999) { Color=vec4(1); return; }
vec3 p=worldPosition(UV,d),n=normalize(texture(Normals,UV).xyz);
vec3 tangent=normalize(cross(n,abs(n.z)<0.9?vec3(0,0,1):vec3(0,1,0))),b=cross(n,tangent);
float occ=0; int count=clamp(data_UBO.Samples,1,32);
for(int i=0;i<32;++i) { if(i>=count) break;
float fi=float(i)+0.5,z=fi/float(count),a=fi*2.399963;
vec3 sampleDirection=tangent*cos(a)*sqrt(1-z*z)+b*sin(a)*sqrt(1-z*z)+n*z;
vec3 samplePos=p+sampleDirection*data_UBO.Radius*(0.1+0.9*z*z);
vec3 projected=projectWorld(samplePos); if(!inside(projected.xy)||projected.z<=0) continue;
float sd=texture(Depth,projected.xy).r; if(sd>=0.999999) continue;
float actual=viewDepth(worldPosition(projected.xy,sd));
float delta=viewDepth(samplePos)-actual;
occ+=step(data_UBO.Bias,delta)*smoothstep(0,1,data_UBO.Radius/max(abs(viewDepth(p)-actual),1e-4));
} Color=vec4(vec3(clamp(1-occ/float(count),0,1)),1);
}
