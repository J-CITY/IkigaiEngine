#version 450
layout(set=0,binding=23) uniform sampler3D ShapeNoise;
layout(set=0,binding=24) uniform sampler3D DetailNoise;
layout(set=0,binding=26) uniform sampler2D CurlNoise;
#define IKIGAI_CLOUD_SHADOW_SHARED_NOISE
#include "../lib/environmentCloudLighting.glsl"
#include "../lib/cloudShadow.glsl"
#include "../lib/environmentCloudDensity.glsl"
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


layout(std140,set=0,binding=6) uniform CloudParameters { vec4 Layer; vec4 Density; vec4 Wind; vec4 SunDirection; vec4 SunColor; vec4 Lighting; vec4 Turbulence; } cloud_UBO;
layout(set=0,binding=25) uniform sampler2D JitterNoise;

vec3 sunDirection() {if(environmentEnabled()) return environmentSun();for(int i=0;i<engine_Lights.lights.length();++i) if(engine_Lights.lights[i].type==1) return normalize(-engine_Lights.lights[i].forward);return normalize(cloud_UBO.SunDirection.xyz);}
vec3 sunRadiance() {if(environmentEnabled()) return environmentSunRadiance();for(int i=0;i<engine_Lights.lights.length();++i) if(engine_Lights.lights[i].type==1) return engine_Lights.lights[i].color*engine_Lights.lights[i].intensity;return cloud_UBO.SunColor.rgb*cloud_UBO.SunDirection.w;}
bool sphereInterval(vec3 origin,vec3 ray,vec3 center,float radius,out vec2 interval) {
vec3 o=origin-center;float b=dot(o,ray),c=dot(o,o)-radius*radius,discriminant=b*b-c;
if(discriminant<0) {interval=vec2(0);return false;}
float root=sqrt(max(discriminant,0));interval=vec2(-b-root,-b+root);return interval.y>0;
}
float cloudDensity(vec3 p) {
if(environmentEnabled()) return environmentDensity(p,ShapeNoise,DetailNoise,CurlNoise);
float r=cloud_UBO.Layer.x;vec3 center=vec3(0,-r,0);
float height=(length(p-center)-r-cloud_UBO.Layer.y)/max(cloud_UBO.Layer.z-cloud_UBO.Layer.y,.001);
if(height<=0 || height>=1 || (environmentEnabled()?engine_Environment.CloudLayer.z:cloud_UBO.Density.x)<=0) return 0;
vec3 wind=vec3(cloud_UBO.Wind.x,0,cloud_UBO.Wind.y)*engine_UBO.Time*cloud_UBO.Wind.z;
vec3 q=p-wind;
q.xz+=textureLod(CurlNoise,q.xz*cloud_UBO.Turbulence.x,0).rg*cloud_UBO.Turbulence.y;
vec4 shape=textureLod(ShapeNoise,q*cloud_UBO.Density.w,0);
float base=clamp((shape.r-(1-clamp(cloud_UBO.Density.x,0,1)))/max(cloud_UBO.Density.x,.001),0,1);
float detail=dot(textureLod(DetailNoise,q*cloud_UBO.Density.z,0).rgb,vec3(.625,.25,.125));
float profile=smoothstep(0,.18,height)*(1-smoothstep(.55,1,height));
return max(base-detail*clamp(cloud_UBO.Wind.w,0,.9),0)*profile*max(cloud_UBO.Density.y,0);
}
float hg(float cosine,float g) {g=clamp(g,-.9,.9);return (1-g*g)/(12.5663706*pow(max(1+g*g-2*g*cosine,1e-4),1.5));}
vec4 marchClouds(vec3 origin,vec3 ray,float limit,out float representativeDistance) {
representativeDistance=0;
float radius=max(cloud_UBO.Layer.x,1),low=max((environmentEnabled()?engine_Environment.CloudLayer.x:cloud_UBO.Layer.y),.001),high=max((environmentEnabled()?engine_Environment.CloudLayer.y:cloud_UBO.Layer.z),low+.001);
vec3 center=vec3(0,-radius,0);vec2 outer,inner,planet;
if(!sphereInterval(origin,ray,center,radius+high,outer)) return vec4(0,0,0,1);
float start=max(outer.x,0),finish=min(outer.y,min(limit,cloud_UBO.Layer.w));
if(sphereInterval(origin,ray,center,radius+low,inner)) {
if(length(origin-center)<radius+low) start=max(start,inner.y);
else if(inner.x>start) finish=min(finish,inner.x);
}
if(sphereInterval(origin,ray,center,radius,planet) && planet.x>0) finish=min(finish,planet.x);
if(finish<=start || (environmentEnabled()?engine_Environment.CloudLayer.z:cloud_UBO.Density.x)<=0) return vec4(0,0,0,1);
int count=environmentCloudViewSteps(cloud_UBO.Lighting.w);float stepLength=(finish-start)/float(count),T=1;
vec3 L=vec3(0),sun=sunDirection(),radiance=sunRadiance();float weightedDistance=0,opacitySum=0;
float phase=mix(hg(dot(ray,sun),-.15),hg(dot(ray,sun),cloud_UBO.SunColor.w),.8);
// Reproducible stratified jitter; no negative ray offset.
vec2 noiseSize=vec2(textureSize(JitterNoise,0));
float jitter=fract(texture(JitterNoise,(gl_FragCoord.xy+vec2(engine_UBO.FrameCount*7,engine_UBO.FrameCount*13))/noiseSize).r+float(engine_UBO.FrameCount%8)*.618034);
vec3 ambient=environmentCloudAmbient();
int lightCount=environmentCloudLightSteps();
// Keep the same optical path when reducing the number of light samples.
float lightStep=cloud_UBO.Lighting.z*(6.0/float(lightCount));
for(int i=0;i<96;++i) {if(i>=count || T<.01) break;
vec3 p=origin+ray*(start+(float(i)+jitter)*stepLength);float density=cloudDensity(p);
if(density<=.00001) continue;
float lightDepth=0;
for(int j=0;j<6;++j) {if(j>=lightCount) break;lightDepth+=cloudDensity(p+sun*(float(j)+.5)*lightStep)*lightStep;}
float opacity=1-exp(-density*stepLength*max(cloud_UBO.Lighting.y,0));
float dark=environmentEnabled()?engine_Environment.CloudShape.y:0;
vec3 light=(ambient*cloud_UBO.Lighting.x+environmentCloudSun(p,radiance)*exp(-lightDepth)*phase)*mix(1,.3,dark);
float distanceWeight=T*opacity;
weightedDistance+=(start+(float(i)+jitter)*stepLength)*distanceWeight;opacitySum+=distanceWeight;
L+=distanceWeight*light;T*=1-opacity;
}representativeDistance=opacitySum>1e-5 ? weightedDistance/opacitySum : 0;
L=environmentCloudAerial(L,T,ray,representativeDistance);
return vec4(L,clamp(T,0,1));
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
float first;vec4 front=marchClouds(engine_UBO.ViewPos,normalize(p-engine_UBO.ViewPos),length(p-engine_UBO.ViewPos),first);
Color=vec4(front.rgb+front.a*max(result,vec3(0)),normal.w);
}
