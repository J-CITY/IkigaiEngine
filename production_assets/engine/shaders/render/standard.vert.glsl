#version 450
// Ported from F:/projects/_/assets_save/gl/standard.vs.glsl
struct LightOGL {
    vec3 pos;
    float cutoff;
    vec3 forward;
    float outerCutoff;
    vec3 color;
    float constant;
    int type;
    float linear;
    float quadratic;
    float intensity;
    float radius;
    float padding[3];
};
layout(std140,set=0,binding=4) uniform PassParameters {
    float dirCascadePlaneDistances[16];
    mat4 dirMatrices[16];
    int engine_ShadowUBO_dirBLOCKER_SEARCH_NUM_SAMPLES;
    int engine_ShadowUBO_dirCascadeCount;
    float engine_ShadowUBO_dirFarPlane;
    float engine_ShadowUBO_dirLIGHT_FRUSTUM_WIDTH;
    float engine_ShadowUBO_dirLIGHT_SIZE;
    float engine_ShadowUBO_dirLIGHT_WORLD_SIZE;
    vec3 engine_ShadowUBO_dirLightDir;
    vec3 engine_ShadowUBO_dirLightPos;
    float engine_ShadowUBO_dirLightShadowStrong;
    mat4 engine_ShadowUBO_dirLightSpaceMatrix;
    float engine_ShadowUBO_dirNEAR_PLANE;
    int engine_ShadowUBO_dirPCF_NUM_SAMPLES;
    mat4 engine_ShadowUBO_spotLightSpaceMatrix;
    bool engine_ShadowUBO_useDirBakedLightShadow;
    bool engine_ShadowUBO_useDirLightShadow;
    bool engine_ShadowUBO_usePointLightShadow;
    bool engine_ShadowUBO_useSpotLightShadow;
    vec4 u_Diffuse;
    bool u_EnableNormalMapping;
    float u_HeightScale;
    float u_Shininess;
    vec3 u_Specular;
    vec2 u_TextureOffset;
    vec2 u_TextureTiling;
} data_UBO;
layout(location=0) in vec3 geo_Pos;
layout(location=1) in vec2 geo_TexCoords;
layout(location=2) in vec3 geo_Normal;
layout(location=3) in vec3 geo_Tangent;
layout(location=4) in vec3 geo_Bitangent;
layout(location=5) in ivec4 boneIds;
layout(location=6) in vec4 weights;

layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4    View;
    mat4    Projection;
    vec3    ViewPos;
    float   Time;
    vec2    ViewportSize;
    float    FPS;
    int      FrameCount;
} engine_UBO;

layout(push_constant) uniform ModelPush { mat4 model; } engine_Model;
layout(location=0) out VS_OUT {
    vec3        FragPos;
    vec3        Normal;
    vec2        TexCoords;
    mat3        TBN;
    flat vec3   TangentViewPos;
    vec3        TangentFragPos;

    vec4 FragPosDirLightSpace;
    vec4 FragPosSpotLightSpace;
    
    vec4 EyeSpacePosition;

    vec4 newPos;
    vec4 prevPos;
} vs_out;

const int MAX_BONES = 128;
const int MAX_BONE_INFLUENCE = 4;
layout(std140,set=0,binding=2) uniform Bones { int use; mat4 bones[128]; } engine_Bones;
#include "lib/morph.glsl"

void main() {
    vec3 morphedPosition = morphPosition(geo_Pos, false);
    vec3 morphedNormal = morphNormal(geo_Normal, false);
    vec3 morphedTangent = morphTangent(geo_Tangent);
    vec3 morphedBitangent = morphBitangent(geo_Normal, geo_Tangent, morphedNormal, morphedTangent, geo_Bitangent);
    vec4 totalPosition = vec4(0.0f);
    if ((engine_Bones.use != 0)) {
        for(int i = 0; i < MAX_BONE_INFLUENCE; i++) {
            if(int(boneIds[i]) == -1) 
                continue;
            if(int(boneIds[i]) >=MAX_BONES) {
                totalPosition = vec4(morphedPosition,1.0f);
                break;
            }
            vec4 localPosition = (engine_Bones.bones[int(boneIds[i])]) * vec4(morphedPosition,1.0f);
            totalPosition += localPosition * weights[i];
        }
    }
    vs_out.TBN = mat3(
        normalize(vec3(engine_Model.model * vec4(morphedTangent,   0.0))),
        normalize(vec3(engine_Model.model * vec4(morphedBitangent, 0.0))),
        normalize(vec3(engine_Model.model * vec4(morphedNormal,    0.0)))
    );

    mat3 TBNi = transpose(vs_out.TBN);
    vec3 PrevFragPos;
    if ((engine_Bones.use != 0)) {
        vs_out.FragPos          = vec3(engine_Model.model * totalPosition);
        PrevFragPos          = vec3(engine_Model.model  * vec4(totalPosition.xyz, 1.0));
        vs_out.EyeSpacePosition =  (engine_UBO.View * engine_Model.model) * totalPosition;
    }
    else {
        vs_out.FragPos          = vec3(engine_Model.model * vec4(morphedPosition, 1.0));
        PrevFragPos          = vec3(engine_Model.model  * vec4(morphedPosition, 1.0));
        vs_out.EyeSpacePosition =  (engine_UBO.View * engine_Model.model) * vec4(morphedPosition, 1.0);
    }
    vs_out.Normal           = normalize(mat3(transpose(inverse(engine_Model.model))) * morphedNormal);
    vs_out.TexCoords        = geo_TexCoords;
    vs_out.TangentViewPos   = TBNi * engine_UBO.ViewPos;
    vs_out.TangentFragPos   = TBNi * vs_out.FragPos;
    vs_out.FragPosDirLightSpace = data_UBO.engine_ShadowUBO_dirLightSpaceMatrix * vec4(vs_out.FragPos, 1.0);
    vs_out.FragPosSpotLightSpace = data_UBO.engine_ShadowUBO_spotLightSpaceMatrix * vec4(vs_out.FragPos, 1.0);

    gl_Position = engine_UBO.Projection * (engine_UBO.View * vec4(vs_out.FragPos, 1.0));
}

