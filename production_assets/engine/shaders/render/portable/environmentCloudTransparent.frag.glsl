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
layout(std140,set=0,binding=5) uniform ShadowData {
mat4 Matrices[11]; vec4 CascadeSplits; vec4 LightIndices;
vec4 PointPositionRange; vec4 Settings;
} engine_ShadowData;
layout(location=0) in vec2 UV; layout(location=1) in vec3 World; layout(location=2) in mat3 TBN;
layout(location=5) in vec4 PreviousClip;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D u_AlbedoMap;
layout(set=0,binding=9) uniform sampler2D u_NormalMap;
layout(set=0,binding=10) uniform sampler2D DirectionalShadow;
layout(set=0,binding=11) uniform sampler2D SpotShadow;
layout(set=0,binding=12) uniform sampler2D PointShadow;
layout(std140,set=0,binding=4) uniform Parameters { vec4 u_Albedo; vec2 u_TextureTiling; vec2 u_TextureOffset; float u_Metallic; float u_Roughness; bool u_EnableNormalMapping; vec3 Ambient; } data_UBO;

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

#include "../lib/surfaceWeather.glsl"
void main() { surfaceCoverage(UV);

vec3 p=World,n=normalize(TBN[2]);
vec2 uv=UV*data_UBO.u_TextureTiling+data_UBO.u_TextureOffset;
vec4 sampleColor=texture(u_AlbedoMap,uv)*data_UBO.u_Albedo;if(sampleColor.a<0.001) discard;
if(data_UBO.u_EnableNormalMapping) n=normalize(TBN*(texture(u_NormalMap,uv).xyz*2-1));
vec4 a=vec4(pow(max(sampleColor.rgb,vec3(0)),vec3(2.2)),clamp(data_UBO.u_Metallic,0,1));float rough=clamp(data_UBO.u_Roughness,.04,1);
surfaceWeather(a.rgb,rough,n,World);
vec3 v=normalize(engine_UBO.ViewPos-p),diffuse,specular,weight;
ibl(n,v,a,rough,1.,diffuse,specular,weight);
vec3 result=diffuse+specular;
for(int i=0;i<engine_Lights.lights.length();++i) {
Light light=engine_Lights.lights[i];
if(light.type==3 || light.type==4) {
bool affected=light.type==3 ? all(lessThanEqual(abs(p-light.pos),abs(vec3(light.constant,light.linear,light.quadratic)))) : distance(p,light.pos)<=light.constant;
if(affected) result+=a.rgb*light.color*light.intensity*1.0;
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

result+=((1-fresnel)*a.rgb*(1-a.a)/3.14159265+spec)*nl*environmentSurfaceLight(p,light.type,i,light.color*light.intensity)*attenuation*shadow;
} float cloudDistance;vec3 cloudRay=normalize(World-engine_UBO.ViewPos);
vec4 front=marchClouds(engine_UBO.ViewPos,cloudRay,length(World-engine_UBO.ViewPos),cloudDistance);
Color=vec4(front.rgb+front.a*environmentFog(max(result,vec3(0)),p,engine_UBO.ViewPos),sampleColor.a);
}
