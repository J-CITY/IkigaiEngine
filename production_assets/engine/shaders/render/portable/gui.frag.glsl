#version 450
layout(location=0) in vec2 UV; layout(location=1) in vec4 Tint;
layout(set=0,binding=8) uniform sampler2D Image; layout(location=0) out vec4 Color;
void main() { Color=texture(Image,UV)*Tint; }
