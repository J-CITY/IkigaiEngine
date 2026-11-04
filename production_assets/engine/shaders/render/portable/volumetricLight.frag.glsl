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
layout(std140,set=0,binding=5) uniform ShadowData {
mat4 Matrices[11]; vec4 CascadeSplits; vec4 LightIndices;
vec4 PointPositionRange; vec4 Settings;
} engine_ShadowData;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(set=0,binding=10) uniform sampler2D DirectionalShadow;
layout(set=0,binding=11) uniform sampler2D SpotShadow;
layout(set=0,binding=12) uniform sampler2D PointShadow;
layout(std140,set=0,binding=4) uniform Parameters { float MaxDistance; float Density; float Anisotropy; float Strength; int Samples; } data_UBO;

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

float shadowPCF(int matrixIndex,vec3 p,sampler2D atlas,vec2 tile,vec2 grid) {
vec4 h=engine_ShadowData.Matrices[matrixIndex]*vec4(p,1);
if(h.w<=0) return 1;
vec3 ndc=h.xyz/h.w; vec2 localUV=ndc.xy*0.5+0.5;
localUV.y=mix(localUV.y,1-localUV.y,engine_Frame.CameraParams.w);
float depth=mix(ndc.z*0.5+0.5,ndc.z,engine_Frame.CameraParams.z);
if(!inside(localUV)||depth<0||depth>1) return 1;
vec2 texel=1.0/vec2(textureSize(atlas,0)); vec2 uv=(localUV+tile)/grid;
vec2 low=tile/grid+texel*0.5,high=(tile+1)/grid-texel*0.5;
float visibility=0;
for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) visibility+=step(depth-engine_ShadowData.Settings.x,texture(atlas,clamp(uv+vec2(x,y)*texel,low,high)).r);
return visibility/9;
}
float shadowFor(int index,vec3 p) {
if(index==int(engine_ShadowData.LightIndices.x)) {
float depth=viewDepth(p); int c=0;
if(depth>engine_ShadowData.CascadeSplits.x)c=1;
if(depth>engine_ShadowData.CascadeSplits.y)c=2;
if(depth>engine_ShadowData.CascadeSplits.z)c=3;
return shadowPCF(c,p,DirectionalShadow,vec2(c%2,c/2),vec2(2));
}
if(index==int(engine_ShadowData.LightIndices.y)) return shadowPCF(4,p,SpotShadow,vec2(0),vec2(1));
if(index==int(engine_ShadowData.LightIndices.z)) {
vec3 v=p-engine_ShadowData.PointPositionRange.xyz,a=abs(v); int face;
if(a.x>=a.y&&a.x>=a.z) face=v.x>=0?0:1;
else if(a.y>=a.z) face=v.y>=0?2:3;
else face=v.z>=0?4:5;
return shadowPCF(5+face,p,PointShadow,vec2(face%3,face/3),vec2(3,2));
} return 1;
}

void main() {
Color=texture(Scene,UV);
int index=int(engine_ShadowData.LightIndices.x); if(index<0 || index>=engine_Lights.lights.length()) return;
float d=texture(Depth,UV).r;vec3 surface=worldPosition(UV,min(d,0.99999));
vec3 ray=surface-engine_UBO.ViewPos;float distance=min(length(ray),data_UBO.MaxDistance);ray=normalize(ray);
vec3 sun=normalize(-engine_Lights.lights[index].forward);float g=clamp(data_UBO.Anisotropy,-.9,.9);
float phase=(1-g*g)/(12.5663706*pow(max(1+g*g-2*g*dot(ray,sun),1e-4),1.5));
float scattering=0;int count=clamp(data_UBO.Samples,1,64);
for(int i=0;i<64;++i) {if(i>=count) break;float t=(float(i)+.5)*distance/float(count);
scattering+=shadowFor(index,engine_UBO.ViewPos+ray*t)*exp(-data_UBO.Density*t)*distance/float(count);}
Color.rgb+=scattering*data_UBO.Density*phase*engine_Lights.lights[index].color*engine_Lights.lights[index].intensity*data_UBO.Strength;
}
