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
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Albedo;
layout(set=0,binding=11) uniform sampler2D Roughness;
layout(set=0,binding=12) uniform sampler2D AO;
layout(set=0,binding=13) uniform sampler2D ScreenShadow;
layout(set=0,binding=14) uniform sampler2D DirectionalShadow;
layout(set=0,binding=15) uniform sampler2D SpotShadow;
layout(set=0,binding=16) uniform samplerCube PointShadow;
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

layout(set=0,binding=20) uniform sampler2D RTVisibility;
void main() {
float d=texture(Depth,UV).r; if(d>=0.999999) { Color=vec4(0); return; }
vec3 p=worldPosition(UV,d),n=normalize(texture(Normals,UV).xyz);
vec4 a=texture(Albedo,UV); float rough=texture(Roughness,UV).r;
vec3 v=normalize(engine_UBO.ViewPos-p),result=vec3(0);
int i=int(engine_CurrentShadow.LightInfo.x); {
Light light=engine_Lights.lights[i];
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
vec4 rt=texture(RTVisibility,UV);float shadow=rt.a>.5?rt.r:shadowSurface(i,p,n);

result+=((1-fresnel)*a.rgb*(1-a.a)/3.14159265+spec)*nl*light.color*light.intensity*attenuation*shadow;
} Color=vec4(max(result,vec3(0)),0);
}
