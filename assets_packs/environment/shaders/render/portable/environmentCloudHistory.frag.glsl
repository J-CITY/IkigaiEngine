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
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Current;
layout(set=0,binding=9) uniform sampler2D Distance;
layout(set=0,binding=10) uniform sampler2D History;
layout(set=0,binding=11) uniform sampler2D PreviousDistance;
layout(std140,set=0,binding=4) uniform Parameters { vec4 Wind; float HistoryWeight; } data_UBO;

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
vec4 current=texture(Current,UV);Color=current;float distance=texture(Distance,UV).r;
if(engine_Frame.FrameState.x<.5 || distance<=0) return;
vec3 ray=normalize(worldPosition(UV,1)-engine_UBO.ViewPos);
vec3 velocity=vec3(data_UBO.Wind.x,0,data_UBO.Wind.y)*data_UBO.Wind.z;
vec3 shift=environmentEnabled()?vec3(engine_Environment.CloudOffset.x-engine_Environment.PreviousCloudOffset.x,0,engine_Environment.CloudOffset.y-engine_Environment.PreviousCloudOffset.y):velocity*engine_Frame.FrameState.y;
vec3 point=engine_UBO.ViewPos+ray*distance-shift;
vec4 clip=engine_Frame.PreviousViewProjection*vec4(point,1);if(clip.w<=0) return;
vec2 uv=clip.xy/clip.w*.5+.5;uv.y=mix(uv.y,1-uv.y,engine_Frame.CameraParams.w);
if(!inside(uv)) return;
float previous=texture(PreviousDistance,uv).r;if(previous<=0 || abs(previous-distance)>max(4,distance*.15)) return;
vec4 low=current,high=current;vec2 texel=1.0/vec2(textureSize(Current,0));
for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {vec4 c=texture(Current,UV+vec2(x,y)*texel);low=min(low,c);high=max(high,c);}
Color=mix(current,clamp(texture(History,uv),low,high),clamp(data_UBO.HistoryWeight,0,.9));
}
