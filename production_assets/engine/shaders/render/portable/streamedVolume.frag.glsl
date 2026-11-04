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
layout(std140,set=0,binding=5) uniform CurrentShadow {
mat4 Matrices[4]; vec4 CascadeSplits; vec4 PositionRange; vec4 Settings; vec4 LightInfo;
} engine_CurrentShadow;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D OpaqueDepth;
layout(set=0,binding=10) uniform sampler2D DirectionalShadow;
layout(set=0,binding=11) uniform sampler2D SpotShadow;
layout(set=0,binding=12) uniform samplerCube PointShadow;
layout(std140,set=0,binding=4) uniform Parameters { float Density; float ScatteringAlbedo; float Anisotropy; float Strength; float MaxDistance; int Samples; } data_UBO;

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
layout(std140,set=0,binding=7) uniform PointShadowParameters {
float Bias; float NormalBias; float Radius; int Samples;
} point_UBO;

float pointVisibility(vec3 p) {
vec3 delta=p-engine_CurrentShadow.PositionRange.xyz;
float d=length(delta),farPlane=engine_CurrentShadow.PositionRange.w;
if(d<max(engine_CurrentShadow.Settings.y,1e-5) || d>=farPlane || farPlane<=0) return 1;
vec3 ray=delta/d;
vec3 tangent=normalize(cross(ray,abs(ray.y)<.95?vec3(0,1,0):vec3(1,0,0)));
vec3 bitangent=cross(ray,tangent);float visibility=0;
int taps=clamp(point_UBO.Samples,1,32);
for(int i=0;i<taps;++i) {
float r=sqrt((float(i)+.5)/float(taps))*max(point_UBO.Radius,0)/d;
float angle=float(i)*2.39996323;
vec3 sampleRay=ray+r*(cos(angle)*tangent+sin(angle)*bitangent);
float stored=textureLod(PointShadow,sampleRay,0).r*farPlane;
visibility+=step(d-max(point_UBO.Bias,0),stored);
} return visibility/float(taps);
}

float shadowPCF(int matrixIndex,vec3 p,sampler2D atlas,vec2 tile,vec2 grid) {
vec4 h=engine_CurrentShadow.Matrices[matrixIndex]*vec4(p,1);
if(h.w<=0) return 1;
vec3 ndc=h.xyz/h.w; vec2 localUV=ndc.xy*0.5+0.5;
localUV.y=mix(localUV.y,1-localUV.y,engine_Frame.CameraParams.w);
float depth=mix(ndc.z*0.5+0.5,ndc.z,engine_Frame.CameraParams.z);
if(!inside(localUV)||depth<0||depth>1) return 1;
vec2 texel=1.0/vec2(textureSize(atlas,0)); vec2 uv=(localUV+tile)/grid;
vec2 low=tile/grid+texel*0.5,high=(tile+1)/grid-texel*0.5;
float visibility=0;
for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) visibility+=step(depth-engine_CurrentShadow.Settings.x,texture(atlas,clamp(uv+vec2(x,y)*texel,low,high)).r);
return visibility/9;
}

float shadowFor(int index,vec3 p) {
if(engine_CurrentShadow.LightInfo.z<.5) return 1;
int type=int(engine_CurrentShadow.LightInfo.y);
if(type==1) {
float depth=viewDepth(p);int c=0;
if(depth>engine_CurrentShadow.CascadeSplits.x)c=1;
if(depth>engine_CurrentShadow.CascadeSplits.y)c=2;
if(depth>engine_CurrentShadow.CascadeSplits.z)c=3;
return shadowPCF(c,p,DirectionalShadow,vec2(c%2,c/2),vec2(2));
}
if(type==2) return shadowPCF(0,p,SpotShadow,vec2(0),vec2(1));
return pointVisibility(p);
}
float shadowSurface(int index,vec3 p,vec3 n) {
return shadowFor(index,p+(int(engine_CurrentShadow.LightInfo.y)==0?n*max(point_UBO.NormalBias,0):vec3(0)));
}

float phase(float c,float g) {return (1-g*g)/(12.5663706*pow(max(1+g*g-2*g*c,.001),1.5));}
void main() {
Color=vec4(0);
if(data_UBO.Density<=0 || data_UBO.Strength<=0 || engine_CurrentShadow.LightInfo.w<.5) return;
vec3 fullDelta=worldPosition(UV,min(texture(OpaqueDepth,UV).r,.999999))-engine_UBO.ViewPos;
float fullDistance=min(length(fullDelta),max(data_UBO.MaxDistance,0));if(fullDistance<1e-5) return;
vec3 ray=normalize(fullDelta);
float distance=min(length(worldPosition(UV,min(texture(Depth,UV).r,.999999))-engine_UBO.ViewPos),fullDistance);
int steps=clamp(data_UBO.Samples,1,64);float ds=fullDistance/float(steps);
float sigma=max(data_UBO.Density,0)*max(data_UBO.Strength,0),T=1;vec3 L=vec3(0);
float g=clamp(data_UBO.Anisotropy,-.9,.9);
float jitter=fract(sin(dot(floor(UV*vec2(textureSize(OpaqueDepth,0))),vec2(12.9898,78.233)))*43758.5453);
int i=int(engine_CurrentShadow.LightInfo.x);Light light=engine_Lights.lights[i];
for(int s=0;s<steps;++s) {
float segment=clamp(distance-float(s)*ds,0,ds);if(segment<=0)break;
float stepT=exp(-sigma*segment);vec3 p=engine_UBO.ViewPos+ray*(float(s)+jitter)*ds;
vec3 deltaLight=light.pos-p;float d=max(length(deltaLight),1e-4);
vec3 l=light.type==1?normalize(-light.forward):deltaLight/d;
float attenuation=light.type==1?1:1/max(light.constant+light.linear*d+light.quadratic*d*d,1e-4);
if(light.type==0) {float farPlane=engine_CurrentShadow.PositionRange.w;attenuation*=1-smoothstep(farPlane*.8,farPlane,d);}
if(light.type==2) {float outer=cos(radians(light.outerCutoff)),inner=cos(radians(light.cutoff));attenuation*=clamp((dot(-l,normalize(light.forward))-outer)/max(inner-outer,1e-4),0,1);}
vec3 incident=light.color*light.intensity*attenuation*shadowFor(i,p)*phase(dot(l,ray),g);
L+=T*(1-stepT)*clamp(data_UBO.ScatteringAlbedo,0,1)*incident;T*=stepT;
} Color=vec4(max(L,vec3(0)),0);
}
