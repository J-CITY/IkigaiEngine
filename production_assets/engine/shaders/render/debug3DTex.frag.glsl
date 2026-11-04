#version 450
// Ported from F:/projects/_/assets_save/gl/debug3DTex.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float far_plane;
    int is3D;
    int isPerspective;
    int isRGB;
    int layer;
    float near_plane;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=8) uniform sampler2DArray depthMap;
layout(set=0,binding=9) uniform sampler3D depthMap3D;

float LinearizeDepth(float depth)
{
    float z = depth * 2.0 - 1.0; 
    return (2.0 * data_UBO.near_plane * data_UBO.far_plane) / (data_UBO.far_plane + data_UBO.near_plane - z * (data_UBO.far_plane - data_UBO.near_plane));	
}

void main() {
    if (data_UBO.isRGB == 0) {
        float depthValue = 0.0;
        if (data_UBO.is3D == 0) {
            depthValue = texture(depthMap, vec3(TexCoords, data_UBO.layer)).r;
        }
        else {
            depthValue = texture(depthMap3D, vec3(TexCoords, data_UBO.layer)).r;
        }
        if (data_UBO.isPerspective == 1) {
            FragColor = vec4(vec3(LinearizeDepth(depthValue) / data_UBO.far_plane), 1.0); 
        }
        else {
            FragColor = vec4(vec3(depthValue), 1.0); 
        }
    }
    else {
        vec3 depthValue = vec3(0.0);
        if (data_UBO.is3D == 0) {
            depthValue = texture(depthMap, vec3(TexCoords, data_UBO.layer)).rgb;
        }
        else {
            depthValue = texture(depthMap3D, vec3(TexCoords, data_UBO.layer)).rgb;
        }
        FragColor = vec4(depthValue, 1.0);
    }
}
