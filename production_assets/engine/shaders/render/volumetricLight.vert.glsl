#version 450
// Ported from shared fullscreen
struct DirLight
{
    
	mat4 transform;
    vec4 color;
    vec3 direction;
};
layout(std140,set=0,binding=4) uniform PassParameters {
    float asymmetry;
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
    DirLight light;
    mat4 lightProj;
    float maxDistance;
    float maxSteps;
    float sampleStep;
    float stepIncrement;
    int voutmat;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=0) out vec2 TexCoords;
void main(){TexCoords=aTexCoords;gl_Position=vec4(aPos,1.0);}
