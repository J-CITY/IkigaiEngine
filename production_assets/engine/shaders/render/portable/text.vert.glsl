#version 450
layout(location=0) in vec3 Position; layout(location=1) in vec2 Texcoord; layout(location=6) in vec4 VertexColor;
layout(std140,set=0,binding=5) uniform GuiData { mat4 Projection; } engine_Gui;
layout(location=0) out vec2 UV; layout(location=1) out vec4 Tint;
void main() { UV=Texcoord; Tint=VertexColor; gl_Position=engine_Gui.Projection*vec4(Position,1); }
