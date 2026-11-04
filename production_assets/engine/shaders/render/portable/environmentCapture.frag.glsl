#version 450
#include "../lib/environment.glsl"
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
layout(set=0,binding=8) uniform sampler2D Equirectangular;
layout(std140,set=0,binding=4) uniform Parameters { int Face; int Mode; float Rotation; float Intensity; vec4 SunDirection; vec4 SunColor; } data_UBO;

vec3 faceDirection(vec2 uv,int face) {
vec2 p=uv*2-1;
if(face==0) return normalize(vec3(1,-p.y,-p.x));
if(face==1) return normalize(vec3(-1,-p.y,p.x));
if(face==2) return normalize(vec3(p.x,1,p.y));
if(face==3) return normalize(vec3(p.x,-1,-p.y));
if(face==4) return normalize(vec3(p.x,-p.y,1));
return normalize(vec3(-p.x,-p.y,-1));
}
// Fullscreen UV already addresses the destination texture on every backend.
// Cubemap face coordinates use that address directly, including the -Y rule.
vec2 canonicalUV(vec2 uv) { return uv; }

vec3 proceduralSky(vec3 direction,vec3 sun,vec3 sunColor,float intensity) {
if(environmentEnabled()) return environmentSky(direction);
float y=clamp(direction.y,0,1);
vec3 color=mix(vec3(.32,.42,.58),vec3(.055,.14,.34),pow(y,.45));
color=mix(vec3(.055,.045,.03),color,smoothstep(-.1,.03,direction.y));
float alignment=clamp(dot(direction,sun),-1,1);
color+=sunColor*intensity*(pow(max(alignment,0),96)*.06+smoothstep(.9998,.99995,alignment));
return color;
}
vec3 rotateY(vec3 d,float angle) {float c=cos(angle),s=sin(angle);return vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}

void main() {
vec3 d=rotateY(faceDirection(canonicalUV(UV),data_UBO.Face),data_UBO.Rotation);
vec3 sun=normalize(data_UBO.SunDirection.xyz),sunColor=data_UBO.SunColor.rgb;float sunIntensity=data_UBO.SunDirection.w;
for(int i=0;i<engine_Lights.lights.length();++i) if(engine_Lights.lights[i].type==1) {sun=normalize(-engine_Lights.lights[i].forward);sunColor=engine_Lights.lights[i].color;sunIntensity=engine_Lights.lights[i].intensity;break;}
vec2 e=vec2(atan(d.z,d.x)/(2*3.14159265)+.5,asin(clamp(d.y,-1,1))/3.14159265+.5);
Color=vec4((data_UBO.Mode==0 ? proceduralSky(d,sun,sunColor,sunIntensity) : texture(Equirectangular,e).rgb)*data_UBO.Intensity,1);
}
