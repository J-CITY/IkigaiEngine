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
layout(set=0,binding=9) uniform sampler2D Reflection;
layout(set=0,binding=10) uniform sampler2D Indirect;
layout(set=0,binding=11) uniform sampler2D Specular;
layout(set=0,binding=12) uniform sampler2D Diffuse;
layout(set=0,binding=13) uniform sampler2D Weight;
layout(set=0,binding=14) uniform sampler2D Albedo;
layout(std140,set=0,binding=4) uniform Parameters { float IndirectWeight; } data_UBO;

void main() {
vec3 scene=texture(Scene,UV).rgb;vec4 reflection=texture(Reflection,UV);
vec4 indirect=texture(Indirect,UV);
float confidence=clamp(reflection.a,0,1),giWeight=clamp(data_UBO.IndirectWeight,0,1)*clamp(indirect.a,0,1);
vec4 albedo=texture(Albedo,UV);
vec3 spec=texture(Specular,UV).rgb,diff=texture(Diffuse,UV).rgb;
vec3 diffuseWeight=albedo.rgb*(1-albedo.a)/3.14159265;
Color=vec4(max(scene-spec*confidence-diff*giWeight+reflection.rgb*texture(Weight,UV).rgb+indirect.rgb*diffuseWeight*giWeight,vec3(0)),1);
}
