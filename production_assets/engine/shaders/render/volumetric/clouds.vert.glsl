#version 450
// Ported from shared fullscreen
struct Ray
{
    vec3 origin;
    vec3 direction;
};
layout(std140,set=0,binding=4) uniform PassParameters {
    vec3 engine_u_SunDir;
    float u_AmbientLightFactor;
    vec3 u_CloudBaseColor;
    float u_CloudCoverage;
    float u_CloudMaxHeight;
    float u_CloudMinHeight;
    vec3 u_CloudTopColor;
    float u_DetailNoiseModifier;
    float u_DetailNoiseScale;
    float u_HenyeyGreensteinGBackward;
    float u_HenyeyGreensteinGForward;
    float u_LightConeRadius;
    float u_LightStepLength;
    float u_MaxNumSteps;
    vec3 u_PlanetCenter;
    float u_PlanetRadius;
    float u_Precipitation;
    float u_ShapeNoiseScale;
    vec3 u_SunColor;
    float u_SunLightFactor;
    float u_TurbulenceAmount;
    float u_TurbulenceNoiseScale;
    float u_WindAngle;
    float u_WindShearOffset;
    float u_WindSpeed;
} data_UBO;
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTexCoords;
layout(location=0) out vec2 TexCoords;
void main(){TexCoords=aTexCoords;gl_Position=vec4(aPos,1.0);}
