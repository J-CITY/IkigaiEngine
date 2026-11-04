#version 450
// Ported from F:/projects/_/assets_save/gl/GBuffer.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float engine_JitterSettings_ditheringScale;
    int engine_JitterSettings_enableDithering;
    float engine_JitterSettings_haltonScale;
    vec2 engine_JitterSettings_haltonSequence[128];
    int engine_JitterSettings_numSamples;
    bool engine_Settings_useTAA;
    mat4 engine_TempData_previousModel;
    mat4 engine_TempData_previousView;
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
layout(location=0) out vec4 gPosition;
layout(location=1) out vec4 gVelocity;
layout(location=2) out vec4 gEyePosition;

void main() {
    
        gPosition = vec4(fs_in.FragPos, 1);
        gEyePosition = fs_in.EyeSpacePosition;
    
    vec2 newPos = ((fs_in.newPos.xy / fs_in.newPos.w) * 0.5 + 0.5);
    vec2 prePos = ((fs_in.prevPos.xy / fs_in.prevPos.w) * 0.5 + 0.5);
    vec2 velocity = (newPos - prePos);
    gVelocity = vec4(velocity, 0, 1);
}
