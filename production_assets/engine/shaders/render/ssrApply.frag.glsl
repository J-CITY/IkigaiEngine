#version 450
// Ported from F:/projects/_/assets_save/gl/ssrApply.fs.glsl

layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

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

layout(set=0,binding=10) uniform sampler2D albedoTex;

layout(set=0,binding=9) uniform sampler2D SSRTex;
layout(set=0,binding=8) uniform sampler2D HDRTex;
layout(set=0,binding=12) uniform sampler2D roughAO;
layout(set=0,binding=11) uniform sampler2D inputTex;

float rand(vec2 co)  {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec3 albedo = texture(albedoTex, TexCoords).rgb;
    
    float metallicFactor = 0.3;
    float roughnessFactor = 0.3;

    float lod = 8.0 * roughnessFactor * roughnessFactor;

    vec3 F0 = mix(vec3(0.04), albedo, metallicFactor);
    
    float r = rand(TexCoords);
    vec2 texelSize = 1.0 / textureSize(SSRTex, 0);

    vec3 initialColor = texture(inputTex, TexCoords).rgb;
    vec3 ssrReflection = texture(SSRTex, TexCoords + sqrt(lod) * texelSize * r, lod).rgb;
    vec3 totalColor = initialColor + F0 * ssrReflection;

    FragColor = vec4(totalColor, 1.0);
    
}
