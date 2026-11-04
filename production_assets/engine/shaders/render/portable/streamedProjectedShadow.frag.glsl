#version 450
layout(location=0) in vec2 UV;
layout(set=0,binding=8) uniform sampler2D u_AlbedoMap;
layout(std140,set=0,binding=4) uniform Parameters { vec4 u_Albedo; vec2 u_TextureTiling; vec2 u_TextureOffset; } data_UBO;

void main() { if(texture(u_AlbedoMap,UV*data_UBO.u_TextureTiling+data_UBO.u_TextureOffset).a*data_UBO.u_Albedo.a<0.5) discard;  }
