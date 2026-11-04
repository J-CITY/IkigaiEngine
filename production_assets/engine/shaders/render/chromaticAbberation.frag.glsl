#version 450
// Ported from F:/projects/_/assets_save/gl/chromaticAbberation.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec3 u_ChromaticAbberationParams;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2D u_Scene;

vec3 applyChromaticAbberation(vec3 color, vec2 texcoord, sampler2D inputTex, float minDistance, float intensity, float distortion) {
    vec2 chromaticAberrationOffset = 2.0f * texcoord - 1.0f;
    float chromaticAberrationOffsetLength = length(chromaticAberrationOffset);
    chromaticAberrationOffsetLength *= distortion;
    float chromaticAberrationTexel = chromaticAberrationOffsetLength - minDistance;

    bool applyChromaticAberration = chromaticAberrationTexel > 0.0f;
    if (applyChromaticAberration)
    {
        chromaticAberrationTexel *= chromaticAberrationTexel;
        chromaticAberrationOffsetLength = max(chromaticAberrationOffsetLength, 0.0001f);

        float multiplier = chromaticAberrationTexel / chromaticAberrationOffsetLength;

        chromaticAberrationOffset *= multiplier * intensity;

        vec2 offsetUV = texcoord - 2.0f * chromaticAberrationOffset;
        color.r = texture(inputTex, offsetUV).r;

        offsetUV = texcoord - chromaticAberrationOffset;
        color.g = texture(inputTex, offsetUV).g;
    }
    return color;
}

void main() {
    vec3 inputColor   = texture(u_Scene, TexCoords).rgb;
    float minDistance = data_UBO.u_ChromaticAbberationParams.x;
    float intensity   = data_UBO.u_ChromaticAbberationParams.y;
    float distortion  = data_UBO.u_ChromaticAbberationParams.z;
    vec3 outputColor  = applyChromaticAbberation(inputColor, TexCoords, u_Scene, minDistance, intensity, distortion);
    FragColor = vec4(outputColor, 1.0f);
}
