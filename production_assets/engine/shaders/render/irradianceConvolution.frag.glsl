#version 450
// Ported from F:/projects/_/assets_save/gl/irradianceConvolution.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 projection;
    mat4 view;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec3 WorldPos;

layout(set=0,binding=8) uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(WorldPos);

    vec3 irradiance = vec3(0.0);   

    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, N);
    up = cross(N, right);
       
    float sampleDelta = 0.025;
    float nrSamples = 0.0f;
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));

            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N; 

            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    FragColor = vec4(irradiance, 1.0);
}
