#version 450
// Ported from F:/projects/_/assets_save/gl/motionBlur.fs.glsl

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
layout(location=0) in vec2 TexCoords;
layout(location=0) out vec4 outColor;

layout(set=0,binding=9) uniform sampler2D velTex;
layout(set=0,binding=8) uniform sampler2D colorTexture;

int   size       = 2;
float separation = 1.0;

void main() {
    vec2 direction = texture(velTex, TexCoords).rg;

    if (length(direction) <= 0.0) {
        outColor = texture(colorTexture, TexCoords);
        
        return; 
    }

    direction.xy *= separation;

    vec2 forward  = TexCoords;
    vec2 backward = TexCoords;

    outColor = texture(colorTexture, TexCoords);
    float count = 1.0;
    for (int i = 0; i < size; ++i) {
        forward  += direction;
        backward -= direction;
        outColor +=texture(colorTexture, forward);
        outColor += texture(colorTexture, backward);
        count += 2.0;
    }

    outColor /= count;

}