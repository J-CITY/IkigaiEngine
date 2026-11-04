#version 450 core

layout(location = 0) in vec3 geo_Pos;
layout(location = 1) in vec2 geo_TexCoords;
layout(location = 2) in vec3 geo_Normal;
layout(location = 3) in vec3 geo_Tangent;
layout(location = 4) in vec3 geo_Bitangent;
layout(location = 5) in ivec4 boneIds;
layout(location = 6) in vec4 weights;

layout(binding = 0) uniform EngineUBO
{
	mat4 View;
	mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} engine_UBO;

layout(push_constant) uniform Constants {
    mat4 model;
} engine_Model;

const int MAX_BONES = 128;

layout (binding = 2) uniform BoneUBO {
    int use;
    mat4 bones[MAX_BONES];
} engine_Bones;

layout(location = 0) out struct { 
    vec3 Normal; 
    vec2 TexCoord;
    vec3 FragPos;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 ViewPos;
    mat3 TBN;
} fs_in;

void main()
{
    mat4 model = engine_Model.model;

    if (engine_Bones.use == 1) {
        mat4 bone_transform = mat4(0.0);
        if (boneIds[0] >= 0) bone_transform += engine_Bones.bones[int(boneIds[0])] * weights[0];
        if (boneIds[1] >= 0) bone_transform += engine_Bones.bones[int(boneIds[1])] * weights[1];
        if (boneIds[2] >= 0) bone_transform += engine_Bones.bones[int(boneIds[2])] * weights[2];
        if (boneIds[3] >= 0) bone_transform += engine_Bones.bones[int(boneIds[3])] * weights[3];
        model = model * bone_transform;
    }

    mat3 _TBN = mat3(
        normalize(vec3(model * vec4(geo_Tangent,   0.0))),
        normalize(vec3(model * vec4(geo_Bitangent, 0.0))),
        normalize(vec3(model * vec4(geo_Normal,    0.0)))
    );
    mat3 TBNi = transpose(_TBN);
    vec3 _FragPos;
    _FragPos = vec3(model * vec4(geo_Pos, 1.0));


	fs_in.FragPos = vec3(model * vec4(geo_Pos, 1.0));
	fs_in.Normal = mat3(transpose(inverse(model))) * geo_Normal;
	fs_in.TexCoord = geo_TexCoords;
    fs_in.TBN = _TBN;
    fs_in.TangentViewPos   = TBNi * engine_UBO.ViewPos;
    fs_in.TangentFragPos   = TBNi * _FragPos;
	gl_Position = engine_UBO.Projection * engine_UBO.View * model * vec4(geo_Pos, 1.0);
    //gl_Position = engine_Model.Projection * engine_Model.View * model * vec4(geo_Pos, 1.0);
}