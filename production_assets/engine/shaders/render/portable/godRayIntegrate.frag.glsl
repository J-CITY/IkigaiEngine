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

bool sunSource(out vec2 source,out vec3 radiance) {
source=vec2(0);radiance=vec3(0);
for(int i=0;i<engine_Lights.lights.length();++i) if(engine_Lights.lights[i].type==1) {
Light light=engine_Lights.lights[i];vec4 h=engine_UBO.Projection*engine_UBO.View*vec4(normalize(-light.forward),0);
if(h.w<=1e-5 || light.intensity<=0) return false;
source=h.xy/h.w*.5+.5;source.y=mix(source.y,1-source.y,engine_Frame.CameraParams.w);
radiance=light.color*light.intensity;
return all(greaterThanEqual(source,vec2(0))) && all(lessThanEqual(source,vec2(1)));
} return false;
}
layout(location=0) in vec2 UV;
layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Mask;
layout(std140,set=0,binding=4) uniform Parameters { float Decay; float Density; int Samples; } data_UBO;

void main() {
Color=vec4(0);vec2 source;vec3 light;if(!sunSource(source,light)) return;
int steps=clamp(data_UBO.Samples,1,96);float decay=clamp(data_UBO.Decay,0,1);
vec2 delta=(UV-source)*clamp(data_UBO.Density,0,1)/float(steps),uv=UV;
vec3 rays=vec3(0);float weight=1,total=0;
for(int i=0;i<steps;++i) {uv-=delta;rays+=texture(Mask,uv).rgb*weight;total+=weight;weight*=decay;}
Color=vec4(rays/max(total,1e-5),1);
}
