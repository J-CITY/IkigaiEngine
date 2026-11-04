#version 450
layout(location=0) in vec3 Position;
layout(location=1) in vec2 Texcoord;
layout(location=2) in vec3 AtlasMin;
layout(location=3) in vec3 AtlasMax;
layout(location=6) in vec4 VertexColor;
layout(std140,set=0,binding=5) uniform GuiData {mat4 Projection;} engine_Gui;
layout(location=0) out vec2 UV;
layout(location=1) out vec4 Tint;
layout(location=2) flat out vec4 GlyphBounds;
void main(){UV=Texcoord;Tint=VertexColor;GlyphBounds=vec4(AtlasMin.xy,AtlasMax.xy);gl_Position=engine_Gui.Projection*vec4(Position,1);}
