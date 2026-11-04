#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;

layout(location=0) in vec3 Position; layout(location=1) in vec2 Texcoord;
layout(location=2) in vec3 Normal; layout(location=3) in vec3 Tangent;
layout(location=4) in vec3 Bitangent; layout(location=5) in ivec4 BoneIds;
layout(location=6) in vec4 Weights;
layout(std140,set=0,binding=2) uniform Bones { int use; mat4 bones[128]; } engine_Bones;
layout(std140,set=0,binding=10) uniform PreviousBones { int use; mat4 bones[128]; } engine_PreviousBones;
layout(push_constant) uniform Model { mat4 model; } engine_Model;
layout(location=0) out vec2 UV; layout(location=1) out vec3 World;
layout(location=2) out mat3 TBN; layout(location=5) out vec4 PreviousClip;
#include "../lib/instances.glsl"
#include "../lib/deformation.glsl"
#include "../lib/morph.glsl"
layout(location=6) out float SurfaceShell;
void main() {
InstanceVariation=instanceVariation();
mat4 skin=mat4(1),previousSkin=mat4(1);
if(engine_Bones.use!=0) {
skin=mat4(0); float sum=0;
for(int i=0;i<4;++i) if(BoneIds[i]>=0 && BoneIds[i]<128 && Weights[i]>0) {
skin+=engine_Bones.bones[BoneIds[i]]*Weights[i]; sum+=Weights[i];
} if(sum<1e-5) skin=mat4(1); else skin/=sum;
}
if(engine_PreviousBones.use!=0) {
previousSkin=mat4(0); float sum=0;
for(int i=0;i<4;++i) if(BoneIds[i]>=0 && BoneIds[i]<128 && Weights[i]>0) {
previousSkin+=engine_PreviousBones.bones[BoneIds[i]]*Weights[i]; sum+=Weights[i];
} if(sum<1e-5) previousSkin=mat4(1); else previousSkin/=sum;
}
SurfaceShell=furShell();
vec3 morphedPosition=morphPosition(Position,false);
vec3 morphedNormal=morphNormal(Normal,false);
vec3 morphedTangent=morphTangent(Tangent);
vec3 morphedBitangent=morphBitangent(Normal,Tangent,morphedNormal,morphedTangent,Bitangent);
vec3 shellPosition=deformLocal(morphedPosition,morphedNormal);
vec4 local=skin*vec4(shellPosition,1); vec4 world=instanceModel()*local;
vec3 localRoot=engine_Surface.Parameters.x>2.5 ? Position : vec3(engine_Surface.Pivot.x,engine_Surface.Shape.y,engine_Surface.Pivot.z);
vec3 root=(instanceModel()*skin*vec4(localRoot,1)).xyz;
vec3 originalWorld=world.xyz;
world.xyz=deformWorld(world.xyz,root,deformationWeight(Position),false);
World=world.xyz; UV=Texcoord;
mat3 normalMatrix=transpose(inverse(mat3(instanceModel()*skin)));
TBN=mat3(normalize(normalMatrix*morphedTangent),normalize(normalMatrix*morphedBitangent),normalize(normalMatrix*morphedNormal));
if(engine_Surface.Parameters.x>.5) {
vec3 before=deformationSafeUnit(originalWorld-root,vec3(0,1,0));
vec3 after=deformationSafeUnit(world.xyz-root,before);
vec3 axis=cross(before,after); float cosine=clamp(dot(before,after),-1,1);
mat3 k=mat3(0,axis.z,-axis.y,-axis.z,0,axis.x,axis.y,-axis.x,0);
TBN=(mat3(1)+k+k*k/max(1+cosine,.001))*TBN;
}
vec3 previousShell=deformLocal(morphPosition(Position,true),morphNormal(Normal,true));
vec3 previousWorld=(instancePreviousModel()*previousSkin*vec4(previousShell,1)).xyz;
vec3 previousRoot=(instancePreviousModel()*previousSkin*vec4(localRoot,1)).xyz;
previousWorld=deformWorld(previousWorld,previousRoot,deformationWeight(Position),true);
PreviousClip=engine_Frame.PreviousViewProjection*vec4(previousWorld,1);
gl_Position=engine_UBO.Projection*engine_UBO.View*world;
}
