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
layout(set=0,binding=8) uniform sampler2D Depth;
layout(set=0,binding=9) uniform sampler2D Clouds;
layout(std140,set=0,binding=4) uniform Parameters { float Radius; } data_UBO;

void main() {
vec2 source;vec3 light;Color=vec4(0);
if(!sunSource(source,light)) return;
vec2 delta=UV-source;delta.x*=engine_UBO.ViewportSize.x/max(engine_UBO.ViewportSize.y,1);
float disk=1-smoothstep(max(data_UBO.Radius*.8,1e-5),max(data_UBO.Radius,2e-5),length(delta));
float visibility=step(.999999,texture(Depth,UV).r)*clamp(texture(Clouds,UV).a,0,1);
Color=vec4(light*disk*visibility,1);
}
