#version 450
// Ported from F:/projects/_/assets_save/gl/ssgi.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float distance;
    float intensity;
    int raySteps;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

const float PI = 3.14159265359;

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

layout(set=0,binding=9) uniform sampler2D inputTex;
layout(set=0,binding=8) uniform sampler2D depthTex;

mat4 viewProjMatrix;

vec4 worldToFragSpace(vec3 v, mat4 viewProj) {
    vec4 proj = viewProj * vec4(v, 1.0f);
    proj.xyz /= proj.w;
    proj.xy = proj.xy * 0.5f + vec2(0.5f);
    return proj;
}

vec3 reconstructWorldPosition(float depth, vec2 texcoord, mat4 invViewProjMatrix) {
    vec4 normPosition = vec4(2.0f * texcoord - vec2(1.0f), depth, 1.0f);
    vec4 worldPosition = invViewProjMatrix * normPosition;
    worldPosition /= worldPosition.w;
    return worldPosition.xyz;
}

float rand(vec2 co)
{
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

float getLOD(vec2 offset)
{
    float v = clamp(pow(dot(offset, offset), 0.1), 0.0, 1.0);
    float lod = 10.0 * v;
    return lod;
}

float calculateShadowRayCast(vec3 startPosition, vec3 endPosition, vec2 startUV)
{
    float rayDistance = length(endPosition - startPosition);
    vec3 rayDirection = normalize(endPosition - startPosition);

    float distancePerStep = rayDistance / data_UBO.raySteps;
    for (int i = 1; i < data_UBO.raySteps; i++)
    {
        float currentDistance = i * distancePerStep;
        vec3 currentPosition = startPosition + rayDirection * currentDistance;

        vec4 projectedPosition = worldToFragSpace(currentPosition, viewProjMatrix);
        vec2 currentUV = projectedPosition.xy;

        float lod = getLOD(currentUV - startUV);
        float projectedDepth = 1.0 / projectedPosition.z;

        float currentDepth = 1.0 / textureLod(depthTex, currentUV, lod).r;
        if (projectedDepth > currentDepth + 0.01)
        {
            return 0.0;
        }
    }

    return 1.0;
}

void main() {
    viewProjMatrix = engine_UBO.Projection * engine_UBO.View;
    mat4 invViewProjMatrix = inverse(viewProjMatrix);

    vec3 position = reconstructWorldPosition(texture(depthTex, TexCoords).x, TexCoords, invViewProjMatrix);
    
    vec3 viewDirection = normalize(engine_UBO.ViewPos - position);

    float randAngle = rand(TexCoords);
    float randSample = -0.7 * rand(TexCoords.yx) + 1.0;
    const int SAMPLES = 4;
    vec3 accum = vec3(0.0);
    for (int i = 0; i < SAMPLES; i++) {
        float sampleDistance = exp(i - SAMPLES) * randSample;
        float phi = ((i + randAngle * SAMPLES) * 2.0 * PI) / SAMPLES;
        vec2 uv = sampleDistance * vec2(cos(phi), sin(phi));

        vec3 lightColor = texture(inputTex, TexCoords + uv).rgb;
        float sampleDepth = textureLod(depthTex, TexCoords + uv, getLOD(uv)).r;
        vec3 lightPosition = reconstructWorldPosition(sampleDepth, TexCoords + uv, invViewProjMatrix);
        vec3 lightDirection = lightPosition - position;

        float currentDistance = length(lightDirection);
        float distanceAttenuation = clamp(1.0 - pow(currentDistance / data_UBO.distance, 4.0), 0.0, 1.0);
        distanceAttenuation = isinf(currentDistance) ? 0.0 : distanceAttenuation;

        float shadowFactor = calculateShadowRayCast(position, lightPosition, TexCoords);

        accum += lightColor * shadowFactor * distanceAttenuation;
    }

    FragColor = vec4(accum * data_UBO.intensity, 1.0);
}
