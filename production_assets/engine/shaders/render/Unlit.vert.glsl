#version 450
// Ported from F:/projects/_/assets_save/gl/Unlit.vs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec4 u_Diffuse;
    vec2 u_TextureOffset;
    vec2 u_TextureTiling;
} data_UBO;
layout(location=0) in vec3 geo_Pos;
layout(location=1) in vec2 geo_TexCoords;
layout(location=2) in vec3 geo_Normal;

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
layout(location=0) out VS_OUT
{
    vec2 TexCoords;
} vs_out;

void main()
{
    vs_out.TexCoords = geo_TexCoords;

    gl_Position = engine_UBO.Projection * engine_UBO.View * engine_Model.model * vec4(geo_Pos, 1.0);
}
