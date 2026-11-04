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
layout(set=0,binding=10) uniform samplerCube Environment;
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Depth;

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

void main() {Color=texture(Scene,UV);float surfaceDepth=texture(Depth,UV).r; if(surfaceDepth<.999999) { Color.rgb=environmentFog(Color.rgb,worldPosition(UV,surfaceDepth),engine_UBO.ViewPos); return; }
vec3 ray=normalize(worldPosition(UV,1)-engine_UBO.ViewPos);Color=vec4(environmentEnabled()?environmentSky(ray):textureLod(Environment,ray,0).rgb,1);}
