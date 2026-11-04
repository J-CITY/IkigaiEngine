#version 450
// Ported from F:/projects/_/assets_save/gl/godRays.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    vec3 u_SunPos;
    bool u_UseGodRays;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=9) uniform sampler2D u_Scene;
layout(set=0,binding=8) uniform sampler2D u_BinaryScene;

const float decay = 0.96815;
const float weight  = 0.587;

void main() {
    vec3 color = texture(u_Scene , TexCoords.xy).xyz;
    
    int NUM_SAMPLES = 80;
	vec2 tc = TexCoords.xy;
	vec2 deltatexCoord = (tc - (data_UBO.u_SunPos.xy*0.5 + 0.5));
	deltatexCoord *= 1.0/ float(NUM_SAMPLES);
	float illuminationDecay = 1.0f;

	vec4 godRayColor = texture(u_BinaryScene , tc.xy)*0.4;
	for(int i = 0 ; i< NUM_SAMPLES ; i++) {
		tc-= deltatexCoord;
		vec4 samp = texture(u_BinaryScene , tc )*0.4;
		samp *= illuminationDecay*weight;
		godRayColor += samp;
		illuminationDecay *= decay;
	}
	vec3 realColor = color;
	color = godRayColor.rrr * 0.3f + realColor;

    int Samples = 128;
    float Intensity = 0.125;
    float Decay = 0.96875;
    vec2 _TexCoord = TexCoords.xy;
    vec2 Direction = vec2(0.5) - _TexCoord;
    Direction /= Samples;
    vec3 Color = texture(u_BinaryScene, _TexCoord).rgb;
    
    for(int Sample = 0; Sample < Samples; Sample++) {
        Color += texture(u_BinaryScene, _TexCoord).rgb * Intensity;
        Intensity *= Decay;
        _TexCoord += Direction;
    }
    color = Color.rrr * 0.3f + color;

    FragColor = vec4(color, 1.0);
}
