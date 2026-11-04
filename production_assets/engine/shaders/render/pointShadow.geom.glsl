#version 450
// Ported from F:/projects/_/assets_save/gl/pointShadow.gs.glsl

layout (triangles) in;
layout (triangle_strip, max_vertices=18) out;

layout(std140,set=0,binding=5) uniform EnginePointShadowUBO {
    vec3 lightPos;
    float far_plane;
    mat4 shadowMatrices[6];
} engine_PointShadowUBO;
layout(location=0) out vec4 FragPos;

void main() {
    for(int face = 0; face < 6; ++face) {
        gl_Layer = face;
        for(int i = 0; i < 3; ++i) {
            FragPos = gl_in[i].gl_Position;
            gl_Position = engine_PointShadowUBO.shadowMatrices[face] * FragPos;
            EmitVertex();
        }    
        EndPrimitive();
    }
}
