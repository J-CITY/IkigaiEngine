#version 450
// Ported from F:/projects/_/assets_save/gl/volumetricLight2.fs.glsl
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
layout(location=0) in vec2 TexCoords;
layout(location=0) out vec4 OutColor;

layout(set=0,binding=8) uniform sampler2D cameraOutput;
layout(set=0,binding=9) uniform sampler2D depthMap;

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

int DirLightCascadeMapCount = 4;

layout(set=0,binding=10) uniform sampler2DArray lightDepthMap;

#define PI 3.1415926535f

vec3 FragPosWorldSpace;

float getTextureLodLevel(vec2 uv)
{

    vec2 dxVtc = dFdx(uv);
    vec2 dyVtc = dFdy(uv);
    float deltaMax2 = max(dot(dxVtc, dxVtc), dot(dyVtc, dyVtc));
    return 0.5 * log2(deltaMax2);
}

float sampleShadowMap(sampler2DArray depthMap, vec2 coords, float lod, float compare)
{
    return step(compare, texture(depthMap, vec3(coords, int(lod))).r);
}

float godRayShadowFactor2D(vec3 coords, sampler2DArray depthMap, float bias)
{
	float compare = coords.z - bias;

	const float lod = getTextureLodLevel(coords.xy);
	vec2 texelSize = textureSize(depthMap, 0).xy;
	vec2 texelSizeInv = 1.0 / texelSize;
	vec2 pixelPos = coords.xy * texelSize + vec2(0.5);
	vec2 fracPart = fract(pixelPos);
	vec2 startTexel = (pixelPos - fracPart) * texelSizeInv;
	float res = sampleShadowMap(depthMap, startTexel + vec2(0.0, 0.0) * texelSizeInv, lod, compare);
	return pow(clamp(res, 0.0, 1.0), 8.0);
}

float godRayShadowSampler(vec3 position, DirLight _local_light, sampler2DArray shadowMap)
{
	const float bias = 0.002;
	vec4 fragLightSpace = data_UBO.lightProj  * vec4(position, 1.0);
	vec3 projectedPosition = fragLightSpace.xyz / fragLightSpace.w;
	if (projectedPosition.x > 0.999 || projectedPosition.x < 0.001 ||
		projectedPosition.y > 0.999 || projectedPosition.y < 0.001 )
	{
		return 0.0;
	}
	return godRayShadowFactor2D(projectedPosition, shadowMap, bias);
}

float phaseHG(float cosTheta)
{
	const float inv4PI = 1.0 / (4.0 * PI);
	float denom = 1 + data_UBO.asymmetry * data_UBO.asymmetry + 2 * data_UBO.asymmetry * cosTheta;
	return inv4PI * (1 - data_UBO.asymmetry * data_UBO.asymmetry) / (denom * sqrt(denom));
}

float noise(vec2 p) 
{
    return fract(sin(dot(p, vec2(123.45, 875.43))) * 5432.3);
}

vec3 reconstructWorldPosition(float depth, vec2 texcoord, mat4 invViewProjMatrix) {
    vec4 normPosition = vec4(2.0f * texcoord - vec2(1.0f), depth, 1.0f);
    vec4 worldPosition = invViewProjMatrix * normPosition;
    worldPosition /= worldPosition.w;
    return worldPosition.xyz;
}

void main() {

    mat4 invViewProjMatrix = inverse(engine_UBO.Projection * engine_UBO.View);
    vec3 _pos = reconstructWorldPosition(texture(depthMap, TexCoords).r, TexCoords, invViewProjMatrix);

	vec3 camera2Frag = (_pos - engine_UBO.ViewPos);
	float fragDistance = length(camera2Frag);
	vec3 fragDirection = normalize(camera2Frag);
	vec3 currentColor = texture(cameraOutput, TexCoords).rgb;
	
	float randomness = noise(TexCoords) * .6;

		float illum = 0.0f;
		float i = 0.0f;
		float stp = data_UBO.sampleStep;
		vec3 pos = engine_UBO.ViewPos;
		for (; i < data_UBO.maxSteps; i++) {
			pos = engine_UBO.ViewPos + stp * fragDirection * (i + randomness);
			if (fragDistance <= distance(pos, engine_UBO.ViewPos))
				break;
			stp *= data_UBO.stepIncrement;

			float shadowFactor = godRayShadowSampler(pos, data_UBO.light, lightDepthMap);

			illum += clamp(shadowFactor, 0.0f, 1.0f);
		}
		illum /= i;
		float distanceTraveled = length(engine_UBO.ViewPos - pos);
		illum *= distanceTraveled / data_UBO.maxDistance;
		illum = clamp(illum, 0.0, 0.6);
		
		float cosTheta = dot(fragDirection, data_UBO.light.direction);
		float scattering = phaseHG(cosTheta);
		scattering = max(scattering, 0.0001);

		currentColor = mix(currentColor, data_UBO.light.color.rgb * scattering, illum);

    OutColor = vec4(currentColor,1.f);

}
