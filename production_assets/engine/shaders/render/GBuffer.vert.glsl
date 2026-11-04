#version 450
// Ported from F:/projects/_/assets_save/gl/GBuffer.vs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float engine_JitterSettings_ditheringScale;
    int engine_JitterSettings_enableDithering;
    float engine_JitterSettings_haltonScale;
    vec2 engine_JitterSettings_haltonSequence[128];
    int engine_JitterSettings_numSamples;
    bool engine_Settings_useTAA;
    mat4 engine_TempData_previousModel;
    mat4 engine_TempData_previousView;
} data_UBO;
layout(location=0) in vec3 geo_Pos;
layout(location=1) in vec2 geo_TexCoords;
layout(location=2) in vec3 geo_Normal;
layout(location=3) in vec3 geo_Tangent;
layout(location=4) in vec3 geo_Bitangent;
layout(location=5) in ivec4 boneIds;
layout(location=6) in vec4 weights;

const int MAX_BONES = 128;
const int MAX_BONE_INFLUENCE = 4;
layout(std140,set=0,binding=2) uniform Bones { int use; mat4 bones[128]; } engine_Bones;

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

void main() {
    vec4 totalPosition = vec4(0.0f);
    if ((engine_Bones.use != 0)) {
        for(int i = 0; i < MAX_BONE_INFLUENCE; i++) {
            if(int(boneIds[i]) == -1) 
                continue;
            if(int(boneIds[i]) >=MAX_BONES) {
                totalPosition = vec4(geo_Pos,1.0f);
                break;
            }
            vec4 localPosition = (engine_Bones.bones[int(boneIds[i])]) * vec4(geo_Pos,1.0f);
            totalPosition += localPosition * weights[i];
        }
    }
    vs_out.TBN = mat3(
        normalize(vec3(engine_Model.model * vec4(geo_Tangent,   0.0))),
        normalize(vec3(engine_Model.model * vec4(geo_Bitangent, 0.0))),
        normalize(vec3(engine_Model.model * vec4(geo_Normal,    0.0)))
    );
    vec3 PrevFragPos;
    mat3 TBNi = transpose(vs_out.TBN);
    if ((engine_Bones.use != 0)) {
        vs_out.FragPos          = vec3(engine_Model.model * totalPosition);
        PrevFragPos          = vec3(data_UBO.engine_TempData_previousModel  * vec4(totalPosition.xyz, 1.0));
        vs_out.EyeSpacePosition =  (engine_UBO.View * engine_Model.model) * totalPosition;
    }
    else {
        vs_out.FragPos          = vec3(engine_Model.model * vec4(geo_Pos, 1.0));
        PrevFragPos          = vec3(data_UBO.engine_TempData_previousModel  * vec4(geo_Pos, 1.0));
        vs_out.EyeSpacePosition =  (engine_UBO.View * engine_Model.model) * vec4(geo_Pos, 1.0);
    }
    vs_out.Normal           = normalize(mat3(transpose(inverse(engine_Model.model))) * geo_Normal);
    vs_out.TexCoords        = geo_TexCoords;
    vs_out.TangentViewPos   = TBNi * engine_UBO.ViewPos;
    vs_out.TangentFragPos   = TBNi * vs_out.FragPos;
    
    mat4 jitterMat = mat4( 1.0 );
    if (data_UBO.engine_Settings_useTAA) {
        uint index = engine_UBO.FrameCount % data_UBO.engine_JitterSettings_numSamples;
        float deltaWidth = 1.0 / engine_UBO.ViewportSize.x;
        float deltaHeight = 1.0 / engine_UBO.ViewportSize.y;
        vec2 jitter = vec2(data_UBO.engine_JitterSettings_haltonSequence[index].x * deltaWidth, 
            data_UBO.engine_JitterSettings_haltonSequence[index].y * deltaHeight);
        if(data_UBO.engine_JitterSettings_haltonScale > 0) {
            jitterMat[3][0] += jitter.x * deltaWidth * data_UBO.engine_JitterSettings_haltonScale;
            jitterMat[3][1] += jitter.y * deltaHeight * data_UBO.engine_JitterSettings_haltonScale;
        }
    }
    gl_Position = jitterMat * (engine_UBO.Projection * (engine_UBO.View * vec4(vs_out.FragPos, 1.0)));

    vs_out.newPos = engine_UBO.Projection * (engine_UBO.View * vec4(vs_out.FragPos, 1.0));
    vs_out.prevPos = engine_UBO.Projection * (data_UBO.engine_TempData_previousView * vec4(PrevFragPos, 1.0)); 
}
