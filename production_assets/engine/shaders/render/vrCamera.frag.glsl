#version 450
// Ported from F:/projects/_/assets_save/gl/vrCamera.fs.glsl

layout(location=0) in vec2 TexCoord;
layout(location=0) out vec4 Color;

layout(set=0,binding=8) uniform sampler2D leftEyeTex;
layout(set=0,binding=9) uniform sampler2D rightEyeTex;

void main() {
    vec3 color = vec3(0.0f);
    vec2 projTexCoord = TexCoord;
    if (projTexCoord.x < 0.5f) {
        projTexCoord.x = projTexCoord.x * 2.0f;
        color = texture(leftEyeTex, projTexCoord).rgb;
    }
    else {
        projTexCoord.x = projTexCoord.x * 2.0f - 1.0f;
        color = texture(rightEyeTex, projTexCoord).rgb;
    }
    Color = vec4(color, 1.0f);
}
