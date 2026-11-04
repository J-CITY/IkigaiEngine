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
layout(set=0,binding=9) uniform sampler2D Normals;
layout(set=0,binding=10) uniform sampler2D Albedo;
layout(set=0,binding=11) uniform sampler2D Roughness;
layout(set=0,binding=12) uniform sampler2D Direct;
layout(set=0,binding=13) uniform samplerCube Irradiance;
layout(set=0,binding=14) uniform samplerCube Prefilter;
layout(set=0,binding=15) uniform sampler2D BrdfLut;

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
void ibl(vec3 n,vec3 v,vec4 albedo,float rough,float ao,out vec3 diffuse,out vec3 specular,out vec3 weight) {
float nv=clamp(dot(n,v),.0001,1.0);vec3 f0=mix(vec3(.04),albedo.rgb,albedo.a);
vec3 f=f0+(max(vec3(1-rough),f0)-f0)*pow(1-nv,5);
vec2 brdf=texture(BrdfLut,vec2(nv,rough)).rg;weight=f0*brdf.x+brdf.y;
diffuse=texture(Irradiance,n).rgb*albedo.rgb*(1-f)*(1-albedo.a)*ao;
specular=textureLod(Prefilter,reflect(-v,n),rough*float(textureQueryLevels(Prefilter)-1)).rgb*weight*ao;
}


void main() {
Color=vec4(0);float d=texture(Depth,UV).r;if(d>=.999999)return;
vec4 normal=texture(Normals,UV);vec3 p=worldPosition(UV,d),n=normalize(normal.xyz),v=normalize(engine_UBO.ViewPos-p);
vec4 a=texture(Albedo,UV);float rough=texture(Roughness,UV).r;vec3 diffuse,specular,weight;
ibl(n,v,a,rough,texture(Roughness,UV).g,diffuse,specular,weight);vec3 result=diffuse+specular+texture(Direct,UV).rgb;
for(int i=0;i<engine_Lights.lights.length();++i) {
Light light=engine_Lights.lights[i];
if(light.type==3 || light.type==4) {
bool affected=light.type==3?all(lessThanEqual(abs(p-light.pos),abs(vec3(light.constant,light.linear,light.quadratic)))):distance(p,light.pos)<=light.constant;
if(affected)result+=a.rgb*light.color*light.intensity;
}}
Color=vec4(max(result,vec3(0)),normal.w);
}
