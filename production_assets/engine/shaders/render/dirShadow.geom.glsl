#version 450
// Ported from F:/projects/_/assets_save/gl/dirShadow.gs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 lightSpaceMatrices[16];
} data_UBO;

layout(triangles, invocations = 5) in;
layout(triangle_strip, max_vertices = 3) out;

void main() {
	for (int i = 0; i < 3; ++i) {
		gl_Position = data_UBO.lightSpaceMatrices[gl_InvocationID] * gl_in[i].gl_Position;
		gl_Layer = gl_InvocationID;
		EmitVertex();
	}
	EndPrimitive();
}
