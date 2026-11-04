#version 450
layout(location=0) in vec2 UV;layout(location=0) out vec4 Color;
layout(set=0,binding=8) uniform sampler2D Input;
layout(set=0,binding=9) uniform sampler2D Weight;
void main(){vec4 reflection=texture(Input,UV);Color=vec4(reflection.a>1e-5?reflection.rgb*texture(Weight,UV).rgb/reflection.a:vec3(0),reflection.a);}
