#version 450
// Ported from F:/projects/_/assets_save/gl/Volumetric/triangle_vs.glsl

layout(location=0) in vec3 aPos;
layout(location=0) out vec2 FS_IN_TexCoord;

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

void main()
{
    FS_IN_TexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position    = vec4(FS_IN_TexCoord * 2.0f - 1.0f, 1.0f, 1.0f);

}

