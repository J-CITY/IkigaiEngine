#version 450
// Ported from F:/projects/_/assets_save/gl/sssApply.fs.glsl

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

layout(set=0,binding=9) uniform sampler2D sssTex;
layout(set=0,binding=8) uniform sampler2D inputTex;

float rand(vec2 co)  {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float shadow = texture(sssTex, TexCoords).r;
    FragColor = vec4(texture(inputTex, TexCoords).rgb * shadow, 1);
}
