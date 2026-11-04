#version 450
// Ported from F:/projects/_/assets_save/gl/pointShadow.fs.glsl

layout(std140,set=0,binding=5) uniform EnginePointShadowUBO {
    vec3 lightPos;
    float far_plane;
    mat4 shadowMatrices[6];
} engine_PointShadowUBO;
layout(location=0) in vec4 FragPos;

void main() {
    float lightDistance = length(FragPos.xyz - engine_PointShadowUBO.lightPos);
    lightDistance = lightDistance / engine_PointShadowUBO.far_plane;
    gl_FragDepth = lightDistance;
}
