#version 450
#include "../lib/thinSurface.glsl"
#include "../lib/cloudShadow.glsl"
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
layout(location=1) out vec4 SpecularIBL;
layout(location=2) out vec4 DiffuseIBL;
layout(location=3) out vec4 SpecularWeight;
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Albedo;
layout(set=0,binding=11) uniform sampler2D Roughness;
layout(set=0,binding=12) uniform sampler2D AO;
layout(set=0,binding=13) uniform sampler2D ScreenShadow;
layout(set=0,binding=14) uniform sampler2D DirectionalShadow;
layout(set=0,binding=15) uniform sampler2D SpotShadow;
layout(set=0,binding=16) uniform sampler2D PointShadow;
layout(std140,set=0,binding=4) uniform Parameters { vec3 Ambient; vec3 Background; } data_UBO;

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

layout(set=0,binding=17) uniform samplerCube Irradiance;
layout(set=0,binding=18) uniform samplerCube Prefilter;
layout(set=0,binding=19) uniform sampler2D BrdfLut;

void ibl(vec3 n,vec3 v,vec4 albedo,float rough,float ao,out vec3 diffuse,out vec3 specular,out vec3 weight) {
float nv=clamp(dot(n,v),.0001,1.0);vec3 f0=mix(vec3(.04),albedo.rgb,albedo.a);
vec3 f=f0+(max(vec3(1-rough),f0)-f0)*pow(1-nv,5);
vec2 brdf=texture(BrdfLut,vec2(nv,rough)).rg;weight=f0*brdf.x+brdf.y;
diffuse=texture(Irradiance,n).rgb*albedo.rgb*(1-f)*(1-albedo.a)*ao;
specular=textureLod(Prefilter,reflect(-v,n),rough*float(textureQueryLevels(Prefilter)-1)).rgb*weight*ao;
}

void main() {
SpecularIBL=vec4(0);DiffuseIBL=vec4(0);SpecularWeight=vec4(0);
float d=texture(Depth,UV).r; if(d>=0.999999) { Color=vec4(data_UBO.Background,1); return; }
vec3 p=worldPosition(UV,d),n=normalize(texture(Normals,UV).xyz);
vec4 a=texture(Albedo,UV); float rough=texture(Roughness,UV).r;
vec3 v=normalize(engine_UBO.ViewPos-p),diffuse,specular,weight;
ibl(n,v,a,rough,(texture(AO,UV).r*texture(Roughness,UV).g),diffuse,specular,weight);
diffuse+=texture(Irradiance,-n).rgb*a.rgb*(1-a.a)*clamp(1-texture(Roughness,UV).a,0,1)*0.3;
vec3 result=diffuse+specular;
SpecularIBL=vec4(specular,1);DiffuseIBL=vec4(diffuse,1);SpecularWeight=vec4(weight,1);
for(int i=0;i<engine_Lights.lights.length();++i) {
Light light=engine_Lights.lights[i];
if(light.type==3 || light.type==4) {
bool affected=light.type==3 ? all(lessThanEqual(abs(p-light.pos),abs(vec3(light.constant,light.linear,light.quadratic)))) : distance(p,light.pos)<=light.constant;
if(affected) result+=a.rgb*light.color*light.intensity*(texture(AO,UV).r*texture(Roughness,UV).g);
continue;
}
if(light.type<0||light.type>2) continue;
vec3 delta=light.pos-p; float distance=max(length(delta),1e-4);
vec3 l=light.type==1?normalize(-light.forward):delta/distance;
float attenuation=light.type==1?1:1/max(light.constant+light.linear*distance+light.quadratic*distance*distance,1e-4);
if(light.type==2) {float outer=cos(radians(light.outerCutoff)),inner=cos(radians(light.cutoff)); attenuation*=clamp((dot(-l,normalize(light.forward))-outer)/max(inner-outer,1e-4),0,1);}
vec3 h=normalize(l+v),f0=mix(vec3(0.04),a.rgb,a.a);
float nl=max(dot(n,l),0),nv=max(dot(n,v),0),nh=max(dot(n,h),0),hv=max(dot(h,v),0);
float alpha=rough*rough,alpha2=alpha*alpha,denom=nh*nh*(alpha2-1)+1;
float distribution=alpha2/max(3.14159265*denom*denom,1e-5);
float k=(rough+1)*(rough+1)/8;
float geometry=(nv/max(nv*(1-k)+k,1e-5))*(nl/max(nl*(1-k)+k,1e-5));
vec3 fresnel=f0+(1-f0)*pow(1-hv,5);
vec3 spec=distribution*geometry*fresnel/max(4*nv*nl,1e-5);
float shadow=shadowFor(i,p);
if(light.type==1 && environmentIsSunLight(i)) shadow*=environmentCloudShadow(p,l);
if(light.type==1) shadow*=texture(ScreenShadow,UV).r;
vec3 transmitted=thinSurfaceLight(a.rgb,a.a,clamp(1-texture(Roughness,UV).a,0,1),n,v,l);
result+=(((1-fresnel)*a.rgb*(1-a.a)/3.14159265+spec)*nl+transmitted)*environmentSurfaceLight(p,light.type,i,light.color*light.intensity)*attenuation*shadow;
} Color=vec4(max(result,vec3(0)),1);
}
