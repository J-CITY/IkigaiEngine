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
layout(set=0,binding=8) uniform sampler2D Scene;
layout(set=0,binding=9) uniform sampler2D Depth;
layout(std140,set=0,binding=4) uniform Parameters { float Decay; float Strength; } data_UBO;

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
Color=texture(Scene,UV);int index=-1;for(int i=0;i<engine_Lights.lights.length();++i) if(engine_Lights.lights[i].type==1) {index=i;break;}
if(index<0) return;Light light=engine_Lights.lights[index];
vec3 source=projectWorld(engine_UBO.ViewPos-normalize(light.forward)*engine_Frame.CameraParams.y);
if(source.z<=0 || !inside(source.xy)) return;
vec2 delta=(source.xy-UV)/32;vec2 uv=UV;float sum=0,weight=1;
for(int i=0;i<32;++i) {uv+=delta;if(!inside(uv)) break;sum+=step(.99999,texture(Depth,uv).r)*weight;weight*=data_UBO.Decay;}
Color.rgb+=light.color*light.intensity*sum*(data_UBO.Strength/32);
}
