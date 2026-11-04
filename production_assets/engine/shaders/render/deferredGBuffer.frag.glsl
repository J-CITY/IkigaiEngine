#version 450
// Ported from F:/projects/_/assets_save/gl/deferredGBuffer.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    bool engine_IsPBR;
    float engine_JitterSettings_ditheringScale;
    int engine_JitterSettings_enableDithering;
    float engine_JitterSettings_haltonScale;
    vec2 engine_JitterSettings_haltonSequence[128];
    int engine_JitterSettings_numSamples;
    bool engine_Settings_useTAA;
    mat4 engine_TempData_previousModel;
    mat4 engine_TempData_previousView;
    vec4 u_Albedo;
    bool u_EnableNormalMapping;
    float u_HeightScale;
    float u_Metallic;
    float u_Roughness;
    float u_Shininess;
    vec3 u_Specular;
    vec2 u_TextureOffset;
    vec2 u_TextureTiling;
} data_UBO;

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
layout(location=0) in VS_OUT {
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
} fs_in;

layout(set=0,binding=8) uniform sampler2D u_AlbedoMap;
layout(set=0,binding=14) uniform sampler2D u_SpecularMap;
layout(set=0,binding=12) uniform sampler2D u_NormalMap;
layout(set=0,binding=10) uniform sampler2D u_HeightMap;
layout(set=0,binding=11) uniform sampler2D u_MetallicMap;
layout(set=0,binding=13) uniform sampler2D u_RoughnessMap;
layout(set=0,binding=9) uniform sampler2D u_AmbientOcclusionMap;

vec3 g_Normal;
vec2 g_TexCoords;
layout(location=0) out vec4 gPosition;
layout(location=1) out vec4 gNormal;
layout(location=2) out vec4 gAlbedoSpec;
layout(location=3) out vec4 gRoughAO;

vec2 ParallaxMapping(vec3 p_ViewDir) {
    const vec2 parallax = p_ViewDir.xy * data_UBO.u_HeightScale * texture(u_HeightMap, g_TexCoords).r;
    return g_TexCoords - vec2(parallax.x, 1.0 - parallax.y);
}

void main() {

    g_TexCoords = data_UBO.u_TextureOffset + vec2(mod(fs_in.TexCoords.x * data_UBO.u_TextureTiling.x, 1), mod(fs_in.TexCoords.y * data_UBO.u_TextureTiling.y, 1));

    if (texture(u_AlbedoMap, g_TexCoords).a * data_UBO.u_Albedo.a < 0.5) discard;
    gPosition = vec4(fs_in.FragPos, 1.0);
    gRoughAO = vec4(0.0, 1.0, 0.0, 1.0);
    if (data_UBO.u_HeightScale > 0.0) {
        
    }
    
    if (data_UBO.u_EnableNormalMapping) {
        g_Normal = texture(u_NormalMap, g_TexCoords).rgb;
        g_Normal = normalize(g_Normal * 2.0 - 1.0);
        g_Normal = normalize(fs_in.TBN * g_Normal);
    }
    else {
        g_Normal = normalize(fs_in.Normal);
    }
    gNormal = vec4(g_Normal, 1);
    gRoughAO.b = abs(fs_in.EyeSpacePosition.z / fs_in.EyeSpacePosition.w);
    if (!data_UBO.engine_IsPBR) {
        gAlbedoSpec.rgb = (texture(u_AlbedoMap, g_TexCoords) * data_UBO.u_Albedo).rgb;
        gAlbedoSpec.a = (texture(u_SpecularMap, g_TexCoords) * vec4(data_UBO.u_Specular, 1.0)).r;
        
        gRoughAO.r = data_UBO.u_Shininess;
    }
    else {
        vec4 albedoRGBA     = texture(u_AlbedoMap, g_TexCoords) * data_UBO.u_Albedo;
        gAlbedoSpec.rgb = pow(albedoRGBA.rgb, vec3(2.2));
        gAlbedoSpec.a = texture(u_MetallicMap, g_TexCoords).r * data_UBO.u_Metallic;
        gRoughAO.r = texture(u_RoughnessMap, g_TexCoords).r * data_UBO.u_Roughness;
        gRoughAO.g = texture(u_AmbientOcclusionMap, g_TexCoords).r;
    }
}
