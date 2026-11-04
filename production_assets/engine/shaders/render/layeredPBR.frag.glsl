#version 450
// Ported from F:/projects/_/assets_save/gl/standardPBR.fs.glsl
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
    vec4 u_Albedo;
    bool u_EnableNormalMapping;
    float u_HeightScale;
    float u_Metallic;
    float u_Roughness;
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

layout(std430,set=0,binding=1) readonly buffer EngineLights { LightOGL lights[]; } engine_Lights;
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
layout(location=0) out vec4 FRAGMENT_COLOR;

layout(set=0,binding=10) uniform sampler2D u_AlbedoMap;
layout(set=0,binding=13) uniform sampler2D u_MetallicMap;
layout(set=0,binding=15) uniform sampler2D u_RoughnessMap;
layout(set=0,binding=11) uniform sampler2D u_AmbientOcclusionMap;
layout(set=0,binding=14) uniform sampler2D u_NormalMap;
layout(set=0,binding=12) uniform sampler2D u_HeightMap;

vec2 g_TexCoords;
vec3 g_Normal;
vec3 g_FragPos;
vec3 toVec3(vec3 inv) {
    return vec3(inv[0], inv[1], inv[2]);
}

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

bool PointInAABB(vec3 p_Point, vec3 p_AabbCenter, vec3 p_AabbHalfSize) {
    return
    (
        p_Point.x > p_AabbCenter.x - p_AabbHalfSize.x && p_Point.x < p_AabbCenter.x + p_AabbHalfSize.x &&
        p_Point.y > p_AabbCenter.y - p_AabbHalfSize.y && p_Point.y < p_AabbCenter.y + p_AabbHalfSize.y &&
        p_Point.z > p_AabbCenter.z - p_AabbHalfSize.z && p_Point.z < p_AabbCenter.z + p_AabbHalfSize.z
    );
}

float LuminosityFromAttenuation(LightOGL p_Light) {
    const vec3  lightPosition   = p_Light.pos;
    const float constant        = p_Light.constant;
    const float linear          = p_Light.linear;
    const float quadratic       = p_Light.quadratic;

    const float distanceToLight = length(lightPosition - fs_in.FragPos);
    const float attenuation     = (constant + linear * distanceToLight + quadratic * (distanceToLight * distanceToLight));
    return 1.0 / attenuation;
}

vec3 CalcAmbientBoxLight(LightOGL p_Light, bool useAO, float ao) {
    const vec3  lightPosition   = p_Light.pos;
    const vec3  lightColor      = p_Light.color;
    float intensity       = p_Light.intensity;
    if (useAO) {
        intensity *= ao;
    }
    const vec3  size            = vec3(p_Light.constant, p_Light.linear, p_Light.quadratic);

    return PointInAABB(fs_in.FragPos, lightPosition, size) ? lightColor * intensity : vec3(0.0);
}

vec3 CalcAmbientSphereLight(LightOGL p_Light, bool useAO, float ao) {
    const vec3  lightPosition   = p_Light.pos;
    const vec3  lightColor      = p_Light.color;
    float intensity       = p_Light.intensity;
    if (useAO) {
        intensity *= ao;
    }
    const float radius          = p_Light.constant;

    return distance(lightPosition, fs_in.FragPos) <= radius ? lightColor * intensity : vec3(0.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

layout(set=0,binding=9) uniform sampler2D engine_dirShadowMap;
layout(set=0,binding=8) uniform sampler2DArray engine_dirBakedShadowMap;

const vec2 Poisson16[16] = vec2[16](
     vec2( -0.94201624, -0.39906216 ),
     vec2( 0.94558609, -0.76890725 ),
     vec2( -0.094184101, -0.92938870 ),
     vec2( 0.34495938, 0.29387760 ),
     vec2( -0.91588581, 0.45771432 ),
     vec2( -0.81544232, -0.87912464 ),
     vec2( -0.38277543, 0.27676845 ),
     vec2( 0.97484398, 0.75648379 ),
     vec2( 0.44323325, -0.97511554 ),
     vec2( 0.53742981, -0.47373420 ),
     vec2( -0.26496911, -0.41893023 ),
     vec2( 0.79197514, 0.19090188 ),
     vec2( -0.24188840, 0.99706507 ),
     vec2( -0.81409955, 0.91437590 ),
     vec2( 0.19984126, 0.78641367 ),
     vec2( 0.14383161, -0.14100790 ) 
);

const vec2 Poisson32[32] = vec2[](
    vec2(-0.975402, -0.0711386),
    vec2(-0.920347, -0.41142),
    vec2(-0.883908, 0.217872),
    vec2(-0.884518, 0.568041),
    vec2(-0.811945, 0.90521),
    vec2(-0.792474, -0.779962),
    vec2(-0.614856, 0.386578),
    vec2(-0.580859, -0.208777),
    vec2(-0.53795, 0.716666),
    vec2(-0.515427, 0.0899991),
    vec2(-0.454634, -0.707938),
    vec2(-0.420942, 0.991272),
    vec2(-0.261147, 0.588488),
    vec2(-0.211219, 0.114841),
    vec2(-0.146336, -0.259194),
    vec2(-0.139439, -0.888668),
    vec2(0.0116886, 0.326395),
    vec2(0.0380566, 0.625477),
    vec2(0.0625935, -0.50853),
    vec2(0.125584, 0.0469069),
    vec2(0.169469, -0.997253),
    vec2(0.320597, 0.291055),
    vec2(0.359172, -0.633717),
    vec2(0.435713, -0.250832),
    vec2(0.507797, -0.916562),
    vec2(0.545763, 0.730216),
    vec2(0.56859, 0.11655),
    vec2(0.743156, -0.505173),
    vec2(0.736442, -0.189734),
    vec2(0.843562, 0.357036),
    vec2(0.865413, 0.763726),
    vec2(0.872005, -0.927));

const vec2 Poisson64[64] = vec2[](
    vec2(-0.934812, 0.366741),
    vec2(-0.918943, -0.0941496),
    vec2(-0.873226, 0.62389),
    vec2(-0.8352, 0.937803),
    vec2(-0.822138, -0.281655),
    vec2(-0.812983, 0.10416),
    vec2(-0.786126, -0.767632),
    vec2(-0.739494, -0.535813),
    vec2(-0.681692, 0.284707),
    vec2(-0.61742, -0.234535),
    vec2(-0.601184, 0.562426),
    vec2(-0.607105, 0.847591),
    vec2(-0.581835, -0.00485244),
    vec2(-0.554247, -0.771111),
    vec2(-0.483383, -0.976928),
    vec2(-0.476669, -0.395672),
    vec2(-0.439802, 0.362407),
    vec2(-0.409772, -0.175695),
    vec2(-0.367534, 0.102451),
    vec2(-0.35313, 0.58153),
    vec2(-0.341594, -0.737541),
    vec2(-0.275979, 0.981567),
    vec2(-0.230811, 0.305094),
    vec2(-0.221656, 0.751152),
    vec2(-0.214393, -0.0592364),
    vec2(-0.204932, -0.483566),
    vec2(-0.183569, -0.266274),
    vec2(-0.123936, -0.754448),
    vec2(-0.0859096, 0.118625),
    vec2(-0.0610675, 0.460555),
    vec2(-0.0234687, -0.962523),
    vec2(-0.00485244, -0.373394),
    vec2(0.0213324, 0.760247),
    vec2(0.0359813, -0.0834071),
    vec2(0.0877407, -0.730766),
    vec2(0.14597, 0.281045),
    vec2(0.18186, -0.529649),
    vec2(0.188208, -0.289529),
    vec2(0.212928, 0.063509),
    vec2(0.23661, 0.566027),
    vec2(0.266579, 0.867061),
    vec2(0.320597, -0.883358),
    vec2(0.353557, 0.322733),
    vec2(0.404157, -0.651479),
    vec2(0.410443, -0.413068),
    vec2(0.413556, 0.123325),
    vec2(0.46556, -0.176183),
    vec2(0.49266, 0.55388),
    vec2(0.506333, 0.876888),
    vec2(0.535875, -0.885556),
    vec2(0.615894, 0.0703452),
    vec2(0.637135, -0.637623),
    vec2(0.677236, -0.174291),
    vec2(0.67626, 0.7116),
    vec2(0.686331, -0.389935),
    vec2(0.691031, 0.330729),
    vec2(0.715629, 0.999939),
    vec2(0.8493, -0.0485549),
    vec2(0.863582, -0.85229),
    vec2(0.890622, 0.850581),
    vec2(0.898068, 0.633778),
    vec2(0.92053, -0.355693),
    vec2(0.933348, -0.62981),
    vec2(0.95294, 0.156896));

const vec2 Poisson100[100] = vec2[](
    vec2(-0.9891574, -0.1059512),
    vec2(-0.9822294, 0.05140843),
    vec2(-0.961332, 0.2562195),
    vec2(-0.9149657, -0.2404464),
    vec2(-0.8896608, -0.4183828),
    vec2(-0.8398135, 0.3748641),
    vec2(-0.8149028, 0.1989844),
    vec2(-0.8046502, 0.5212684),
    vec2(-0.7970151, -0.5834194),
    vec2(-0.7484995, -0.3153634),
    vec2(-0.738582, -0.09323367),
    vec2(-0.695694, 0.08865929),
    vec2(-0.6868832, 0.6336682),
    vec2(-0.6751406, 0.2777427),
    vec2(-0.666558, -0.6801786),
    vec2(-0.631489, -0.4702293),
    vec2(-0.5870083, 0.518836),
    vec2(-0.5744062, -0.06333278),
    vec2(-0.5667221, 0.1699501),
    vec2(-0.5537653, 0.7677022),
    vec2(-0.5337034, 0.3299558),
    vec2(-0.5201509, -0.2033358),
    vec2(-0.4873925, -0.8545401),
    vec2(-0.4712743, -0.3607009),
    vec2(-0.4524891, -0.5142469),
    vec2(-0.4421883, -0.6830674),
    vec2(-0.4293752, 0.6299667),
    vec2(-0.4240644, 0.8706763),
    vec2(-0.4139857, 0.1598689),
    vec2(-0.3838707, 0.4078749),
    vec2(-0.3688077, -0.0358762),
    vec2(-0.3432877, -0.2311365),
    vec2(-0.3256257, -0.9325441),
    vec2(-0.2751555, 0.302412),
    vec2(-0.2679778, -0.654425),
    vec2(-0.2554769, -0.4441924),
    vec2(-0.243476, -0.8034022),
    vec2(-0.2367678, -0.108045),
    vec2(-0.2196257, 0.8243803),
    vec2(-0.2119443, 0.06230118),
    vec2(-0.1708038, -0.9437978),
    vec2(-0.1694005, 0.5692244),
    vec2(-0.136494, 0.3937041),
    vec2(-0.1318274, -0.2166154),
    vec2(-0.09781472, -0.5743775),
    vec2(-0.09480921, 0.2369129),
    vec2(-0.07638182, -0.0571501),
    vec2(-0.06661344, -0.7966294),
    vec2(-0.06305461, -0.3521975),
    vec2(-0.04525706, 0.6982157),
    vec2(-0.04149697, 0.9666064),
    vec2(-0.003192461, -0.9693027),
    vec2(0.0104818, 0.5000805),
    vec2(0.03228819, -0.1681713),
    vec2(0.03715288, -0.673852),
    vec2(0.08470399, -0.3922319),
    vec2(0.09848712, -0.8374477),
    vec2(0.09940207, 0.1117471),
    vec2(0.1395643, 0.313647),
    vec2(0.1565993, 0.8555924),
    vec2(0.1772605, -0.5248074),
    vec2(0.1899546, 0.5249656),
    vec2(0.1952665, -0.9595091),
    vec2(0.213078, -0.07045701),
    vec2(0.2277649, -0.3361143),
    vec2(0.247221, 0.7353553),
    vec2(0.2493455, -0.6874771),
    vec2(0.269915, 0.07673722),
    vec2(0.3039587, 0.9087375),
    vec2(0.3189922, 0.3008468),
    vec2(0.3215453, -0.1954931),
    vec2(0.3593478, 0.4527411),
    vec2(0.3745022, -0.597945),
    vec2(0.3879738, -0.7821383),
    vec2(0.4522015, 0.6819367),
    vec2(0.4591872, -0.4484442),
    vec2(0.4626173, -0.03955235),
    vec2(0.4751598, 0.2083394),
    vec2(0.4894366, 0.8694122),
    vec2(0.4896614, -0.2676601),
    vec2(0.5070116, -0.6733028),
    vec2(0.5525513, 0.436241),
    vec2(0.5542312, -0.8262905),
    vec2(0.6012187, 0.7003717),
    vec2(0.6075609, -0.1610506),
    vec2(0.6291932, 0.2213627),
    vec2(0.6300695, -0.5324634),
    vec2(0.6613995, -0.7056449),
    vec2(0.6699739, -0.3828001),
    vec2(0.6705787, 0.01011722),
    vec2(0.6814164, 0.5618623),
    vec2(0.7808329, 0.261445),
    vec2(0.7830279, -0.1817809),
    vec2(0.8006546, -0.5266678),
    vec2(0.8030878, 0.4266291),
    vec2(0.8259325, 0.08734058),
    vec2(0.8621388, -0.3646038),
    vec2(0.9531851, 0.3011991),
    vec2(0.9578334, -0.1584408),
    vec2(0.9898114, 0.1029227));

const vec2 Poisson128[128] = vec2[](
    vec2(-0.9406119, 0.2160107),
    vec2(-0.920003, 0.03135762),
    vec2(-0.917876, -0.2841548),
    vec2(-0.9166079, -0.1372365),
    vec2(-0.8978907, -0.4213504),
    vec2(-0.8467999, 0.5201505),
    vec2(-0.8261013, 0.3743192),
    vec2(-0.7835162, 0.01432008),
    vec2(-0.779963, 0.2161933),
    vec2(-0.7719588, 0.6335353),
    vec2(-0.7658782, -0.3316436),
    vec2(-0.7341912, -0.5430729),
    vec2(-0.6825727, -0.1883408),
    vec2(-0.6777467, 0.3313724),
    vec2(-0.662191, 0.5155144),
    vec2(-0.6569989, -0.7000636),
    vec2(-0.6021447, 0.7923283),
    vec2(-0.5980815, -0.5529259),
    vec2(-0.5867089, 0.09857152),
    vec2(-0.5774597, -0.8154474),
    vec2(-0.5767041, -0.2656419),
    vec2(-0.575091, -0.4220052),
    vec2(-0.5486979, -0.09635002),
    vec2(-0.5235587, 0.6594529),
    vec2(-0.5170338, -0.6636339),
    vec2(-0.5114055, 0.4373561),
    vec2(-0.4844725, 0.2985838),
    vec2(-0.4803245, 0.8482798),
    vec2(-0.4651957, -0.5392771),
    vec2(-0.4529685, 0.09942394),
    vec2(-0.4523471, -0.3125569),
    vec2(-0.4268422, 0.5644538),
    vec2(-0.4187512, -0.8636028),
    vec2(-0.4160798, -0.0844868),
    vec2(-0.3751733, 0.2196607),
    vec2(-0.3656596, -0.7324334),
    vec2(-0.3286595, -0.2012637),
    vec2(-0.3147397, -0.0006635741),
    vec2(-0.3135846, 0.3636878),
    vec2(-0.3042951, -0.4983553),
    vec2(-0.2974239, 0.7496996),
    vec2(-0.2903037, 0.8890813),
    vec2(-0.2878664, -0.8622097),
    vec2(-0.2588971, -0.653879),
    vec2(-0.2555692, 0.5041648),
    vec2(-0.2553292, -0.3389159),
    vec2(-0.2401368, 0.2306108),
    vec2(-0.2124457, -0.09935001),
    vec2(-0.1877905, 0.1098409),
    vec2(-0.1559879, 0.3356432),
    vec2(-0.1499449, 0.7487829),
    vec2(-0.146661, -0.9256138),
    vec2(-0.1342774, 0.6185387),
    vec2(-0.1224529, -0.3887629),
    vec2(-0.116467, 0.8827716),
    vec2(-0.1157598, -0.539999),
    vec2(-0.09983152, -0.2407187),
    vec2(-0.09953719, -0.78346),
    vec2(-0.08604223, 0.4591112),
    vec2(-0.02128129, 0.1551989),
    vec2(-0.01478849, 0.6969455),
    vec2(-0.01231739, -0.6752576),
    vec2(-0.005001599, -0.004027164),
    vec2(0.00248426, 0.567932),
    vec2(0.00335562, 0.3472346),
    vec2(0.009554717, -0.4025437),
    vec2(0.02231783, -0.1349781),
    vec2(0.04694207, -0.8347212),
    vec2(0.05412609, 0.9042216),
    vec2(0.05812819, -0.9826952),
    vec2(0.1131321, -0.619306),
    vec2(0.1170737, 0.6799788),
    vec2(0.1275105, 0.05326218),
    vec2(0.1393405, -0.2149568),
    vec2(0.1457873, 0.1991508),
    vec2(0.1474208, 0.5443151),
    vec2(0.1497117, -0.3899909),
    vec2(0.1923773, 0.3683496),
    vec2(0.2110928, -0.7888536),
    vec2(0.2148235, 0.9586087),
    vec2(0.2152219, -0.1084362),
    vec2(0.2189204, -0.9644538),
    vec2(0.2220028, -0.5058427),
    vec2(0.2251696, 0.779461),
    vec2(0.2585723, 0.01621339),
    vec2(0.2612841, -0.2832426),
    vec2(0.2665483, -0.6422054),
    vec2(0.2939872, 0.1673226),
    vec2(0.3235748, 0.5643662),
    vec2(0.3269232, 0.6984669),
    vec2(0.3425438, -0.1783788),
    vec2(0.3672505, 0.4398117),
    vec2(0.3755714, -0.8814359),
    vec2(0.379463, 0.2842356),
    vec2(0.3822978, -0.381217),
    vec2(0.4057849, -0.5227674),
    vec2(0.4168737, -0.6936938),
    vec2(0.4202749, 0.8369391),
    vec2(0.4252189, 0.03818182),
    vec2(0.4445904, -0.09360636),
    vec2(0.4684285, 0.5885228),
    vec2(0.4952184, -0.2319764),
    vec2(0.5072351, 0.3683765),
    vec2(0.5136194, -0.3944138),
    vec2(0.519893, 0.7157083),
    vec2(0.5277841, 0.1486474),
    vec2(0.5474944, -0.7618791),
    vec2(0.5692734, 0.4852227),
    vec2(0.582229, -0.5125455),
    vec2(0.583022, 0.008507785),
    vec2(0.6500257, 0.3473313),
    vec2(0.6621304, -0.6280518),
    vec2(0.6674218, -0.2260806),
    vec2(0.6741871, 0.6734863),
    vec2(0.6753459, 0.1119422),
    vec2(0.7083091, -0.4393666),
    vec2(0.7106963, -0.102099),
    vec2(0.7606754, 0.5743545),
    vec2(0.7846709, 0.2282225),
    vec2(0.7871446, 0.3891495),
    vec2(0.8071781, -0.5257092),
    vec2(0.8230689, 0.002674922),
    vec2(0.8531976, -0.3256475),
    vec2(0.8758298, -0.1824844),
    vec2(0.8797691, 0.1284946),
    vec2(0.926309, 0.3576975),
    vec2(0.9608918, -0.03495717),
    vec2(0.972032, 0.2271516));

int g_Layer = 0;

float PenumbraSize(float zReceiver, float zBlocker) 
{
    return (zReceiver - zBlocker) / zBlocker;
}

void FindBlocker(
        out float avgBlockerDepth,
        out int numBlockers,
        vec2      uv,
        float     bias,
        float     zReceiver) 
{
    float blockerSum = 0;
    float searchWidth = data_UBO.engine_ShadowUBO_dirLIGHT_SIZE * (zReceiver - data_UBO.engine_ShadowUBO_dirNEAR_PLANE) / zReceiver;
    for(int i = 0; i < data_UBO.engine_ShadowUBO_dirBLOCKER_SEARCH_NUM_SAMPLES; i++) {
        vec2 offset = Poisson64[i];
        
        float sampleDepth = texture(engine_dirShadowMap, uv + offset *  searchWidth).r;
        
        if(sampleDepth < (zReceiver - bias)) {
            blockerSum += sampleDepth;
            numBlockers++;
        }
    }
    avgBlockerDepth = blockerSum / float(numBlockers);
}

float PCF_Filter(
      vec2  uv,
      float zReceiver,
      float bias,   
      float filter_radius) 
{
    float sum = 0.0;
    for(int i = 0; i < data_UBO.engine_ShadowUBO_dirPCF_NUM_SAMPLES; i++) {
        vec2 offset = Poisson64[i] * filter_radius;
        
        float sampleDepth = texture(engine_dirShadowMap, uv + offset).r;
        if(sampleDepth < (zReceiver - bias)) {
            sum += 1.0;
        }
    }
    return sum / float(data_UBO.engine_ShadowUBO_dirPCF_NUM_SAMPLES);
}

float PCSS(vec2 uv, float bias, float zReceiver) {

    float avgBlockerDepth = 0.0f;
    int numBlockers = 0;
    FindBlocker(avgBlockerDepth, numBlockers, uv, bias, zReceiver);

    if(numBlockers == 0) {
        return 1.0;
    }
    
     float penumbraRatio = PenumbraSize(zReceiver, avgBlockerDepth);
     float filterRadiusUV = penumbraRatio * data_UBO.engine_ShadowUBO_dirLIGHT_SIZE * data_UBO.engine_ShadowUBO_dirNEAR_PLANE / zReceiver; 
    
    return PCF_Filter(uv, zReceiver, bias ,filterRadiusUV);
}

void FindBlockerCascade(
        out float avgBlockerDepth,
        out int numBlockers,
        vec3      uv,
        float     bias,
        float     zReceiver) 
{
    float blockerSum = 0;
    float searchWidth = data_UBO.engine_ShadowUBO_dirLIGHT_SIZE * (zReceiver - data_UBO.engine_ShadowUBO_dirNEAR_PLANE) / zReceiver;
    for(int i = 0; i < data_UBO.engine_ShadowUBO_dirBLOCKER_SEARCH_NUM_SAMPLES; i++) {
        vec2 offset = Poisson64[i];
        
        float sampleDepth = texture(engine_dirBakedShadowMap, uv + vec3(offset, 0) *  searchWidth).r;
        
        if(sampleDepth < (zReceiver - bias)) {
            blockerSum += sampleDepth;
            numBlockers++;
        }
    }
    avgBlockerDepth = blockerSum / float(numBlockers);
}

float PCF_FilterCascade(
      vec3  uv,
      float zReceiver,
      float bias,   
      float filter_radius) 
{
    float sum = 0.0;
    for(int i = 0; i < data_UBO.engine_ShadowUBO_dirPCF_NUM_SAMPLES; i++) {
        vec2 offset = Poisson64[i] * filter_radius;
        
        float sampleDepth = texture(engine_dirBakedShadowMap, uv + vec3(offset, 0)).r;
        if(sampleDepth < (zReceiver - bias)) {
            sum += 1.0;
        }
    }
    return sum / float(data_UBO.engine_ShadowUBO_dirPCF_NUM_SAMPLES);
}

float PCSSCascade(vec3 uv, float bias, float zReceiver) {

    float avgBlockerDepth = 0.0f;
    int numBlockers = 0;
    FindBlockerCascade(avgBlockerDepth, numBlockers, uv, bias, zReceiver);

    if(numBlockers == 0) {
        return 1.0;
    }
    
     float penumbraRatio = PenumbraSize(zReceiver, avgBlockerDepth);
     float filterRadiusUV = penumbraRatio * data_UBO.engine_ShadowUBO_dirLIGHT_SIZE * data_UBO.engine_ShadowUBO_dirNEAR_PLANE / zReceiver; 
    
    return PCF_FilterCascade(uv, zReceiver, bias ,filterRadiusUV);
}

float ShadowCalculationWithCascade(vec3 fragPosWorldSpace) {
    
    vec4 fragPosViewSpace = engine_UBO.View * vec4(fragPosWorldSpace, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;
    for (int i = 0; i < data_UBO.engine_ShadowUBO_dirCascadeCount; ++i) {
        if (depthValue < data_UBO.dirCascadePlaneDistances[i])
        {
            layer = i;
            break;
        }
    }
    if (layer == -1) {
        layer = data_UBO.engine_ShadowUBO_dirCascadeCount;
    }

    vec4 fragPosLightSpace = data_UBO.dirMatrices[layer] * vec4(fragPosWorldSpace, 1.0);
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    projCoords = projCoords * 0.5 + 0.5;

    float currentDepth = projCoords.z;

    if (currentDepth > 1.0) {
        return 0.0;
    }
    
    vec3 normal = normalize(g_Normal);
    float bias = max(0.05 * (1.0 - dot(normal, data_UBO.engine_ShadowUBO_dirLightDir)), 0.005);
    const float biasModifier = 0.5f;
    if (layer == data_UBO.engine_ShadowUBO_dirCascadeCount) {
        bias *= 1 / (data_UBO.engine_ShadowUBO_dirFarPlane * biasModifier);
    }
    else {
        bias *= 1 / (data_UBO.dirCascadePlaneDistances[layer] * biasModifier);
    }

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(engine_dirBakedShadowMap, 0).xy);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(engine_dirBakedShadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, layer)).r;
            
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }    
    }
    shadow /= 17.0;
    return shadow;
}

float DirShadowCalculation(vec4 fragPosLightSpace)
{
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    projCoords = projCoords * 0.5 + 0.5;

    float closestDepth = texture(engine_dirShadowMap, projCoords.xy).r; 

    float currentDepth = projCoords.z;

    vec3 normal = normalize(g_Normal);
    vec3 lightDir = normalize(data_UBO.engine_ShadowUBO_dirLightPos - g_FragPos);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.001);
	
    if (projCoords.x > 1.0 || projCoords.y > 1.0 || projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.y < 0.0 || projCoords.z < 0.0)
        return 0.0;

    float shadow = PCSS(projCoords.xy, bias, currentDepth);

    if(projCoords.z > 1.0) {
        shadow = 0.0;
    }
    return shadow;
}

vec2 ParallaxMapping(vec3 p_ViewDir) {
    const vec2 parallax = p_ViewDir.xy * data_UBO.u_HeightScale * texture(u_HeightMap, g_TexCoords).r;
    return g_TexCoords - vec2(parallax.x, 1.0 - parallax.y);
}

#include "lib/pbrLayers.glsl"

void main() {
    PbrSurface surface = EvaluatePbrSurface(fs_in.TexCoords);
    if (PbrDiscard(surface.opacity)) discard;
    vec4 albedoRGBA = vec4(surface.color, surface.opacity);
    vec3 albedo = surface.color;
    float metallic = surface.metallic;
    float roughness = surface.roughness;
    float ao = surface.ao;
    vec3 normal = normalize(fs_in.TBN * surface.normal);

    vec3 N = normalize(normal);
    g_Normal = N;
    g_FragPos = fs_in.FragPos;
    vec3 V = normalize(engine_UBO.ViewPos - fs_in.FragPos);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    vec3 ambientSum = vec3(0.0);

    for (int i = 0; i < engine_Lights.lights.length(); ++i)  {
        if (int(engine_Lights.lights[i].type) == 3) {
            ambientSum += CalcAmbientBoxLight(engine_Lights.lights[i], false, 1.0);
        }
        else if (int(engine_Lights.lights[i].type) == 4) {
            ambientSum += CalcAmbientSphereLight(engine_Lights.lights[i], false, 1.0);
        }
        else {
            
            vec3 L = int(engine_Lights.lights[i].type) == 1 ? -engine_Lights.lights[i].forward : normalize(engine_Lights.lights[i].pos - fs_in.FragPos);
            vec3 H = normalize(V + L);
            float distance    = length(engine_Lights.lights[i].pos - fs_in.FragPos);
            float lightCoeff = 0.0;

            float shadow = 0.0f;
            switch(int(engine_Lights.lights[i].type)) {
                case 0:
                    lightCoeff = LuminosityFromAttenuation(engine_Lights.lights[i]) * engine_Lights.lights[i].intensity;
                    break;

                case 1:
                    if (data_UBO.engine_ShadowUBO_useDirLightShadow) {
                        shadow = DirShadowCalculation(fs_in.FragPosDirLightSpace);
                    }
                    lightCoeff = engine_Lights.lights[i].intensity;
                    break;

                case 2:
                    const vec3  lightForward    = engine_Lights.lights[i].forward;
                    const float cutOff          = cos(radians(engine_Lights.lights[i].cutoff));
                    const float outerCutOff     = cos(radians(engine_Lights.lights[i].cutoff + engine_Lights.lights[i].outerCutoff));

                    const vec3  lightDirection  = normalize(engine_Lights.lights[i].pos - fs_in.FragPos);
                    const float luminosity      = LuminosityFromAttenuation(engine_Lights.lights[i]);

                    const float theta           = dot(lightDirection, normalize(-lightForward)); 
                    const float epsilon         = cutOff - outerCutOff;
                    const float spotIntensity   = clamp((theta - outerCutOff) / epsilon, 0.0, 1.0);

                    lightCoeff = luminosity * spotIntensity * engine_Lights.lights[i].intensity;
                    break;
            }

            vec3 radiance = engine_Lights.lights[i].color * lightCoeff;

            float NDF = DistributionGGX(N, H, roughness);
            float G   = GeometrySmith(N, V, L, roughness);
            vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
            
            vec3 kS = F;
            vec3 kD = vec3(1.0) - kS;
            kD *= 1.0 - metallic;
            
            vec3 numerator    = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
            vec3 specular     = numerator / max(denominator, 0.001);  

            float NdotL = max(dot(N, L), 0.0);
            
            vec3 resLight = (kD * albedo / PI + specular) * radiance * NdotL;
            resLight *= (1.0 - shadow);
            Lo += resLight; 
        }
    }

    vec3 ambient = ambientSum * albedo * ao;
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  
   
    FRAGMENT_COLOR = vec4(color, albedoRGBA.a);
}