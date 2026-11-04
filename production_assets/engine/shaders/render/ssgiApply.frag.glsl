#version 450
// Ported from F:/projects/_/assets_save/gl/ssgiApply.fs.glsl

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

layout(set=0,binding=10) uniform sampler2D inputTex;
layout(set=0,binding=8) uniform sampler2D SSGITex;
layout(set=0,binding=9) uniform sampler2D albedoTex;

void main() {
    vec3 originalColor = texture(inputTex, TexCoords, 0).rgb;
    vec3 ssgiColor = texture(SSGITex, TexCoords, 0).rgb;

    vec3 albedo = texture(albedoTex, TexCoords).rgb;
    
    float metallic = 0.3;
    vec3 color = mix(albedo, originalColor, metallic * 0.5);

    FragColor = vec4(originalColor + color * ssgiColor, 1.0);
}
