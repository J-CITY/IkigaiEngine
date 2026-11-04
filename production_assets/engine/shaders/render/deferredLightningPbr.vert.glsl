#version 450
// Ported from F:/projects/_/assets_save/gl/deferredLightningPbr.vs.glsl
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
struct DATA {
    vec3        FragPos;
    vec3        Normal;
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
    bool engine_UseSSAO;
    bool u_UseIbl;
} data_UBO;
layout(location=0) in vec3 geo_Pos;
layout(location=1) in vec2 geo_TexCoords;
layout(location=0) out vec2 TexCoords;

void main() {
    TexCoords = geo_TexCoords;
    gl_Position = vec4(geo_Pos, 1.0);
}
