#version 450
// Ported from F:/projects/_/assets_save/gl/standardLines.vs.glsl

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

const int MAX_BONES = 128;
const int MAX_BONE_INFLUENCE = 4;
layout(std140,set=0,binding=2) uniform Bones { int use; mat4 bones[128]; } engine_Bones;

void main() {
    vec3 FragPos;
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
    if ((engine_Bones.use != 0)) {
        FragPos = vec3(engine_Model.model * totalPosition);
    }
    else {
        FragPos = vec3(engine_Model.model * vec4(geo_Pos, 1.0));
    }
   
    gl_Position = engine_UBO.Projection * (engine_UBO.View * vec4(FragPos, 1.0));
}
