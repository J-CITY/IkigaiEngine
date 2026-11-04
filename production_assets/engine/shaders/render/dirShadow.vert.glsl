#version 450
// Ported from F:/projects/_/assets_save/gl/dirShadow.vs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 lightSpaceMatrices[16];
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

void main() {
    gl_Position = engine_Model.model * vec4(geo_Pos, 1.0);
}
