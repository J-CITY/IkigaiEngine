#version 450
// Ported from F:/projects/_/assets_save/gl/ssao.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    mat4 projection;
    vec3 samples[64];
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=9) uniform sampler2D gPosition;
layout(set=0,binding=8) uniform sampler2D gNormal;
layout(set=0,binding=10) uniform sampler2D gTexNoise;

int kernelSize = 64;
float radius = 0.5;
float bias = 0.025;

const vec2 noiseScale = vec2(800.0/4.0, 600.0/4.0); 

void main() {
    
    vec3 fragPos = texture(gPosition, TexCoords).xyz;
    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);
    vec3 randomVec = normalize(texture(gTexNoise, TexCoords * noiseScale).xyz);

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for(int i = 0; i < kernelSize; ++i) {
        
        vec3 _sample = TBN * data_UBO.samples[i];
        _sample = fragPos + _sample * radius; 

        vec4 offset = vec4(_sample, 1.0);
        offset = data_UBO.projection * offset; 
        offset.xyz /= offset.w; 
        offset.xyz = offset.xyz * 0.5 + 0.5; 

        float sampleDepth = texture(gPosition, offset.xy).z; 

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= _sample.z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    occlusion = 1.0 - (occlusion / kernelSize);
    
    FragColor = vec4(occlusion);
}

