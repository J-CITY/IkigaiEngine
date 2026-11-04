#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=5) uniform ShadowPass { mat4 ViewProjection; } engine_ShadowPass;

layout(location=0) in vec3 Position; layout(location=1) in vec2 Texcoord;
layout(location=2) in vec3 Normal; layout(location=3) in vec3 Tangent;
layout(location=4) in vec3 Bitangent; layout(location=5) in ivec4 BoneIds;
layout(location=6) in vec4 Weights;
layout(std140,set=0,binding=2) uniform Bones { int use; mat4 bones[128]; } engine_Bones;
layout(push_constant) uniform Model { mat4 model; } engine_Model;
layout(location=0) out vec2 UV; layout(location=1) out vec3 World;
layout(location=2) out mat3 TBN; layout(location=5) out vec4 PreviousClip;
#include "../lib/morph.glsl"
#include "../lib/instances.glsl"
void main() {
InstanceVariation=instanceVariation();
mat4 skin=mat4(1);
if(engine_Bones.use!=0) {
skin=mat4(0); float sum=0;
for(int i=0;i<4;++i) if(BoneIds[i]>=0 && BoneIds[i]<128 && Weights[i]>0) {
skin+=engine_Bones.bones[BoneIds[i]]*Weights[i]; sum+=Weights[i];
} if(sum<1e-5) skin=mat4(1); else skin/=sum;
}
vec3 morphedPosition=morphPosition(Position,false);
vec3 morphedNormal=morphNormal(Normal,false);
vec3 morphedTangent=morphTangent(Tangent);
vec3 morphedBitangent=morphBitangent(Normal,Tangent,morphedNormal,morphedTangent,Bitangent);
vec4 local=skin*vec4(morphedPosition,1); vec4 world=instanceModel()*local;
World=world.xyz; UV=Texcoord;
mat3 normalMatrix=transpose(inverse(mat3(instanceModel()*skin)));
TBN=mat3(normalize(normalMatrix*morphedTangent),normalize(normalMatrix*morphedBitangent),normalize(normalMatrix*morphedNormal));
PreviousClip=engine_Frame.PreviousViewProjection*instancePreviousModel()*skin*vec4(morphPosition(Position,true),1);
gl_Position=engine_ShadowPass.ViewProjection*world;
}
