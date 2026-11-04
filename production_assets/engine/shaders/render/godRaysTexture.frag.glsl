#version 450
// Ported from F:/projects/_/assets_save/gl/godRaysTexture.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec3 u_Color;
} data_UBO;
layout(location=0) in VS_OUT {
	vec3 color;
} fs_in;
layout(location=0) out vec4 FRAGMENT_COLOR;

void main() {
	FRAGMENT_COLOR = vec4(fs_in.color, 1.0);
}
