#version 450
// Ported from F:/projects/_/assets_save/gl/fog.fs.glsl
struct FogParameters {
	vec3 color;
	float linearStart;
	float linearEnd;
	float density;
	
	int equation;
	bool isEnabled;
};
layout(std140,set=0,binding=4) uniform PassParameters {
    FogParameters u_engine_FogParams;
} data_UBO;
layout(location=0) out vec4 FragColor;
layout(location=0) in vec2 TexCoords;

layout(set=0,binding=9) uniform sampler2D u_Scene;
layout(set=0,binding=8) uniform sampler2D positionTexture;

float getFogFactor(FogParameters params, float fogCoordinate) {
	float result = 0.0;
	if(params.equation == 0) {
		float fogLength = params.linearEnd - params.linearStart;
		result = (params.linearEnd - fogCoordinate) / fogLength;
	}
	else if(params.equation == 1) {
		result = exp(-params.density * fogCoordinate);
	}
	else if(params.equation == 2) {
		result = exp(-pow(params.density * fogCoordinate, 2.0));
	}
	result = 1.0 - clamp(result, 0.0, 1.0);
	return result;
}

void main() {
    FragColor = texture(u_Scene, TexCoords);
	if(data_UBO.u_engine_FogParams.isEnabled) {
      vec4 ioEyeSpacePosition = texture(positionTexture, TexCoords);
      float fogCoordinate = abs(ioEyeSpacePosition.z / ioEyeSpacePosition.w);
      FragColor = mix(FragColor, vec4(data_UBO.u_engine_FogParams.color, 1.0), getFogFactor(data_UBO.u_engine_FogParams, fogCoordinate));
    }
}
