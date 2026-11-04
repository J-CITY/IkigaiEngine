#version 450
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
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(std140,set=0,binding=4) uniform Parameters { float FocusDistance; float FocusRange; float Radius; } data_UBO;

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
float d=texture(Depth,UV).r; vec3 center=texture(Scene,UV).rgb;
float distance=d>=0.999999?engine_Frame.CameraParams.y:viewDepth(worldPosition(UV,d));
float coc=clamp(abs(distance-data_UBO.FocusDistance)/max(data_UBO.FocusRange,0.01),0,1);
vec2 radius=vec2(data_UBO.Radius*coc)/vec2(textureSize(Scene,0)); vec3 sum=center; float count=1;
for(int i=0;i<12;++i) {float angle=float(i)*2.399963;vec2 uv=UV+vec2(cos(angle),sin(angle))*radius*sqrt((float(i)+1)/12.0);
if(!inside(uv))continue;sum+=texture(Scene,uv).rgb;count+=1;}
Color=vec4(sum/count,1);
}
