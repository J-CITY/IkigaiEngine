#version 450
// Ported from F:/projects/_/assets_save/gl/Unlit.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec4 u_Diffuse;
    vec2 u_TextureOffset;
    vec2 u_TextureTiling;
} data_UBO;
layout(location=0) out vec4 FRAGMENT_COLOR;
layout(location=0) in VS_OUT
{
    vec2 TexCoords;
} fs_in;

layout(set=0,binding=8) uniform sampler2D u_DiffuseMap;

void main()
{
    FRAGMENT_COLOR = texture(u_DiffuseMap, data_UBO.u_TextureOffset + vec2(mod(fs_in.TexCoords.x * data_UBO.u_TextureTiling.x, 1), mod(fs_in.TexCoords.y * data_UBO.u_TextureTiling.y, 1))) * data_UBO.u_Diffuse;
}