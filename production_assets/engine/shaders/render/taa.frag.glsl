#version 450
// Ported from F:/projects/_/assets_save/gl/taa.fs.glsl

layout(std140,set=0,binding=4) uniform PassParameters {
    float engine_TaaSettings_feedbackFactor;
    float engine_TaaSettings_maxDepthFalloff;
    float engine_TaaSettings_velocityScale;
} data_UBO;

layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4    View;
    mat4    Projection;
    vec3    ViewPos;
    float   Time;
    vec2    ViewportSize;
    float    FPS;
    int      FrameCount;
} engine_UBO;

layout(push_constant) uniform ModelPush { mat4 model; } engine_Model;
layout(location=0) in vec2 TexCoords;
layout(location=0) out vec4 outColor;

layout(set=0,binding=8) uniform sampler2D currentColorTex;
layout(set=0,binding=9) uniform sampler2D currentDepthTex;
layout(set=0,binding=10) uniform sampler2D previousColorTex;
layout(set=0,binding=11) uniform sampler2D previousDepthTex;
layout(set=0,binding=12) uniform sampler2D velocityTex;

const vec2 kOffsets3x3[9] =
{
	vec2(-1, -1), 
	vec2( 0, -1), 
	vec2( 1, -1), 
	vec2(-1,  0), 
	vec2( 0,  0), 
	vec2( 1,  0), 
	vec2(-1,  1), 
	vec2( 0,  1), 
	vec2( 1,  1), 
}; 

const uint kNeighborsCount = 9;

const vec2 kOffsets2x2[5] =
{
	vec2(-1, 0), 
	vec2(0, -1), 
	vec2( 0,  0), 
	vec2(1, 0), 
	vec2(0, 1) 
}; 

const uint neighborCount = 5;

vec2 GetClosestUV(in sampler2D depths)
{
	vec2 deltaRes = vec2(1.0 / engine_UBO.ViewportSize.x, 1.0 / engine_UBO.ViewportSize.y);
	vec2 closestUV = TexCoords;
	float closestDepth = 1.0f;

	for(uint iter = 0; iter < kNeighborsCount; iter++)
	{
		vec2 newUV = TexCoords + (kOffsets3x3[iter] * deltaRes);

		float depth = texture(depths, newUV).x;

		if(depth < closestDepth)
		{
			closestDepth = depth;
			closestUV = newUV;
		}
	}

	return closestUV;
}

vec2 MinMaxDepths(in float neighborDepths[kNeighborsCount])
{
	float minDepth = neighborDepths[0];
	float maxDepth = neighborDepths[0];

	for(int iter = 1; iter < kNeighborsCount; iter++)
	{
		minDepth = min(minDepth, neighborDepths[iter]);
		maxDepth = max(maxDepth, neighborDepths[iter]);
	}

	return vec2(minDepth, maxDepth);
}

vec4 MinColors(in vec4 neighborColors[neighborCount])
{
	vec4 minColor = neighborColors[0];

	for(int iter = 1; iter < neighborCount; iter++)
	{
		minColor = min(minColor, neighborColors[iter]);
	}

	return minColor;
}

vec4 MaxColors(in vec4 neighborColors[neighborCount])
{
	vec4 maxColor = neighborColors[0];

	for(int iter = 1; iter < neighborCount; iter++)
	{
		maxColor = max(maxColor, neighborColors[iter]);
	}

	return maxColor;
}

vec4 MinColors2(in vec4 neighborColors[kNeighborsCount])
{
	vec4 minColor = neighborColors[0];

	for(int iter = 1; iter < neighborCount; iter++)
	{
		minColor = min(minColor, neighborColors[iter]);
	}

	return minColor;
}

vec4 MaxColors2(in vec4 neighborColors[kNeighborsCount])
{
	vec4 maxColor = neighborColors[0];

	for(int iter = 1; iter < neighborCount; iter++)
	{
		maxColor = max(maxColor, neighborColors[iter]);
	}

	return maxColor;
}

vec4 ConstrainHistory(vec4 neighborColors[neighborCount])
{
	vec4 previousColorMin = MinColors(neighborColors);
	vec4 previousColorMax = MaxColors(neighborColors);

	return clamp(neighborColors[2], previousColorMin, previousColorMax);
}

vec4 clip_aabb(vec3 colorMin, vec3 colorMax, vec4 currentColor, vec4 previousColor)
{
	vec3 p_clip = 0.5 * (colorMax + colorMin);
	vec3 e_clip = 0.5 * (colorMax - colorMin);
	vec4 v_clip = previousColor - vec4(p_clip, currentColor.a);
	vec3 v_unit = v_clip.rgb / e_clip;
	vec3 a_unit = abs(v_unit);
	float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));
	if (ma_unit > 1.0)
	{
		return vec4(p_clip, currentColor.a) + v_clip / ma_unit;
	}
	else
	{
		return previousColor;
	}
}

vec4 Inside2Resolve(sampler2D currColorTex, sampler2D prevColorTex, vec2 velocity)
{
	vec2 deltaRes = vec2(1.0 / engine_UBO.ViewportSize.x, 1.0 / engine_UBO.ViewportSize.y);

	vec4 current3x3Colors[kNeighborsCount];
	vec4 previous3x3Colors[kNeighborsCount];

	for(uint iter = 0; iter < kNeighborsCount; iter++)
	{
		vec2 newUV = TexCoords + (kOffsets3x3[iter] * deltaRes);

		current3x3Colors[iter] = texture(currColorTex, newUV);

		previous3x3Colors[iter] = texture(prevColorTex, newUV + velocity);
	}

	vec4 rounded3x3Min = MinColors2(current3x3Colors);
	vec4 rounded3x3Max = MaxColors2(previous3x3Colors);

	vec4 current2x2Colors[neighborCount];
	vec4 previous2x2Colors[neighborCount];
	for(uint iter = 0; iter < neighborCount; iter++)
	{
		vec2 newUV = TexCoords + (kOffsets2x2[iter] * deltaRes);

		current2x2Colors[iter] = texture(currColorTex, newUV);

		previous2x2Colors[iter] = texture(prevColorTex, newUV + velocity);
	}

	vec4 min2 = MinColors(current2x2Colors);
	vec4 max2 = MaxColors(previous2x2Colors);

	vec4 mixedMin = mix(rounded3x3Min, min2, 0.5);
	vec4 mixedMax = mix(rounded3x3Max, max2, 0.5);

	float testVel = data_UBO.engine_TaaSettings_feedbackFactor - (length(velocity) * data_UBO.engine_TaaSettings_velocityScale);
	return mix(current2x2Colors[2], clip_aabb(mixedMin.rgb, mixedMax.rgb, current2x2Colors[2], ConstrainHistory(previous2x2Colors)), testVel);
}

vec4 Custom2Resolve(in float preNeighborDepths[kNeighborsCount], in float curNeighborDepths[kNeighborsCount], vec2 velocity)
{
	
	vec2 preMinMaxDepths = MinMaxDepths(preNeighborDepths);
	vec2 curMinMaxDepths = MinMaxDepths(curNeighborDepths);

	float highestDepth = min(preMinMaxDepths.x, curMinMaxDepths.x); 
	float lowestDepth = max(preMinMaxDepths.x, curMinMaxDepths.x); 

	float depthFalloff = abs(highestDepth - lowestDepth);

	vec4 res = vec4(0);

	vec4 taa = Inside2Resolve(currentColorTex, previousColorTex, velocity);

	float averageDepth = 0;
	for(uint iter = 0; iter < kNeighborsCount; iter++)
	{
		averageDepth += curNeighborDepths[iter];
	}

	averageDepth /= kNeighborsCount;

	if(averageDepth < data_UBO.engine_TaaSettings_maxDepthFalloff)
	{
		res = taa;
	}

	else
	{
		res = texture(currentColorTex, TexCoords);
	}

	return res;
}

void main()
{
	float currentDepths[kNeighborsCount];
	float previousDepths[kNeighborsCount];

	vec2 deltaRes = vec2(1.0 / engine_UBO.ViewportSize.x, 1.0 / engine_UBO.ViewportSize.y);

	vec2 closestVec = -texture(velocityTex, GetClosestUV(currentDepthTex)).rg;

	for(uint iter = 0; iter < kNeighborsCount; iter++)
	{
		vec2 newUV = TexCoords + (kOffsets3x3[iter] * deltaRes);

		currentDepths[iter] = texture(currentDepthTex, newUV).x;
		previousDepths[iter] = texture(previousDepthTex, newUV + closestVec).x;
	}

	outColor = Custom2Resolve(previousDepths, currentDepths, closestVec);
}