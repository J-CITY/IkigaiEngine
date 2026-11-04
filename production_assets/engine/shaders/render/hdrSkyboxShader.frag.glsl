#version 450
// Ported from F:/projects/_/assets_save/gl/hdrSkyboxShader.fs.glsl

layout(location=0) out vec4 FragColor;
layout(location=0) in vec3 WorldPos;

layout(set=0,binding=8) uniform samplerCube environmentMap;

void main() {
    vec3 envColor = textureLod(environmentMap, WorldPos, 0.0).rgb;
    
    envColor = envColor / (envColor + vec3(1.0));
    envColor = pow(envColor, vec3(1.0/2.2)); 
    FragColor = vec4(envColor, 1.0);
}
