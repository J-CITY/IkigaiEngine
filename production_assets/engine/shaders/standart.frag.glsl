#version 450 core

// OUTPUT
layout(location = 0) out vec4 FRAGMENT_COLOR;

// INPUT
layout(location = 0) in struct { 
    vec3 Normal; 
    vec2 TexCoord;
    vec3 FragPos;
    vec3 TangentViewPos;
    vec3 TangentFragPos;
    vec3 ViewPos;
    mat3 TBN;
} fs_in;

// UNIFORMS
layout(binding = 0) uniform EngineUBO
{
    mat4 View;
    mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} engine_UBO;

struct LightOGL {
    vec3 pos;
    float cutoff;
    vec3 forward;
    float outerCutoff;
    vec3 color;
    float constant;
    int type;
    float linear;
    float quadratic;
    float intensity;
    float radius;
};

layout(std430, binding = 3) buffer EngineLights {
    LightOGL lights[];
} engine_Lights;

layout(std140, binding = 4) uniform DataUBO {
    vec2        u_TextureTiling;
    vec2        u_TextureOffset;
    vec4        u_Diffuse;
    vec3        u_Specular;
    float       u_Shininess;
    float       u_HeightScale;
    bool        u_EnableNormalMappingBool;
} data_UBO;

uniform layout(binding = 5) sampler2D u_DiffuseMap;
uniform layout(binding = 6) sampler2D u_SpecularMap;
uniform layout(binding = 7) sampler2D u_NormalMap;
uniform layout(binding = 8) sampler2D u_HeightMap;

// GLOBAL VARS
vec3 g_Normal;
vec2 g_TexCoords;
vec3 g_ViewDir;
vec4 g_DiffuseTexel;
vec4 g_SpecularTexel;
vec4 g_HeightTexel;
vec4 g_NormalTexel;
float g_Shininess;
vec3 g_FragPos;

bool PointInAABB(vec3 p_Point, vec3 p_AabbCenter, vec3 p_AabbHalfSize) {
    return
    (
        p_Point.x > p_AabbCenter.x - p_AabbHalfSize.x && p_Point.x < p_AabbCenter.x + p_AabbHalfSize.x &&
        p_Point.y > p_AabbCenter.y - p_AabbHalfSize.y && p_Point.y < p_AabbCenter.y + p_AabbHalfSize.y &&
        p_Point.z > p_AabbCenter.z - p_AabbHalfSize.z && p_Point.z < p_AabbCenter.z + p_AabbHalfSize.z
    );
}

vec3 BlinnPhong(vec3 p_LightDir, vec3 p_LightColor, float p_Luminosity, float shadow)
{
    const vec3  halfwayDir          = normalize(p_LightDir + g_ViewDir);
    const float diffuseCoefficient  = max(dot(g_Normal, p_LightDir), 0.0);
    const float specularCoefficient = pow(max(dot(g_Normal, halfwayDir), 0.0), g_Shininess * 2.0);

    return (1.0-shadow) * (p_LightColor * g_DiffuseTexel.rgb * diffuseCoefficient * p_Luminosity + 
        ((p_Luminosity > 0.0) ? (p_LightColor * g_SpecularTexel.rgb * specularCoefficient * p_Luminosity) : vec3(0.0)));
}

float LuminosityFromAttenuation(LightOGL p_Light)
{
    const vec3  lightPosition   = vec3(p_Light.pos[0], p_Light.pos[1], p_Light.pos[2]);
    const float constant        = p_Light.constant;
    const float linear          = p_Light.linear;
    const float quadratic       = p_Light.quadratic;

    const float distanceToLight = length(lightPosition - fs_in.FragPos);
    const float attenuation     = (constant + linear * distanceToLight + quadratic * (distanceToLight * distanceToLight));
    return 1.0 / attenuation;
}
vec3 CalcPointLight(LightOGL p_Light, float shadow)
{
    const vec3 lightPosition  = vec3(p_Light.pos[0], p_Light.pos[1], p_Light.pos[2]);
    if (distance(lightPosition, fs_in.FragPos) <= p_Light.radius) {
        return vec3(0);
    }
    const vec3 lightColor     = vec3(p_Light.color[0], p_Light.color[1], p_Light.color[2]);
    const float intensity     = p_Light.intensity;

    const vec3  lightDirection  = normalize(lightPosition - fs_in.FragPos);
    const float luminosity      = LuminosityFromAttenuation(p_Light);

    return BlinnPhong(lightDirection, lightColor, intensity * luminosity, shadow);
}

vec3 CalcDirectionalLight(LightOGL p_Light, float shadow)
{
    return BlinnPhong(-vec3(p_Light.forward[0], p_Light.forward[1], p_Light.forward[2]), 
        vec3(p_Light.color[0], p_Light.color[1], p_Light.color[2]), p_Light.intensity, shadow);
}

vec3 CalcSpotLight(LightOGL p_Light, float shadow)
{
    const vec3  lightPosition   = vec3(p_Light.pos[0], p_Light.pos[1], p_Light.pos[2]);
    if (distance(lightPosition, fs_in.FragPos) <= p_Light.radius) {
        return vec3(0);
    }
    const vec3  lightForward    = vec3(p_Light.forward[0], p_Light.forward[1], p_Light.forward[2]);
    const vec3  lightColor      = vec3(p_Light.color[0], p_Light.color[1], p_Light.color[2]);
    const float intensity       = p_Light.intensity;
    const float cutOff          = cos(radians(p_Light.cutoff));
    const float outerCutOff     = cos(radians(p_Light.cutoff + p_Light.outerCutoff));

    const vec3  lightDirection  = normalize(lightPosition - fs_in.FragPos);
    const float luminosity      = LuminosityFromAttenuation(p_Light);

    /* Calculate the spot intensity */
    const float theta           = dot(lightDirection, normalize(-lightForward)); 
    const float epsilon         = cutOff - outerCutOff;
    const float spotIntensity   = clamp((theta - outerCutOff) / epsilon, 0.0, 1.0);
    
    return BlinnPhong(lightDirection, lightColor, intensity * spotIntensity * luminosity, shadow);
}

vec3 CalcAmbientBoxLight(LightOGL p_Light, bool useAO, float ao) {
    const vec3  lightPosition   = vec3(p_Light.pos[0], p_Light.pos[1], p_Light.pos[2]);
    const vec3  lightColor      = vec3(p_Light.color[0], p_Light.color[1], p_Light.color[2]);
    float intensity             = p_Light.intensity;
    if (useAO) {
        intensity *= ao;
    }
    const vec3  size            = vec3(p_Light.constant, p_Light.linear, p_Light.quadratic);

    return PointInAABB(fs_in.FragPos, lightPosition, size) ? g_DiffuseTexel.rgb * lightColor * intensity : vec3(0.0);
}

vec3 CalcAmbientSphereLight(LightOGL p_Light, bool useAO, float ao) {
    const vec3  lightPosition   = vec3(p_Light.pos[0], p_Light.pos[1], p_Light.pos[2]);
    const vec3  lightColor      = vec3(p_Light.color[0], p_Light.color[1], p_Light.color[2]);
    float intensity       = p_Light.intensity;
    if (useAO) {
        intensity *= ao;
    }
    const float radius          = p_Light.constant;

    return distance(lightPosition, fs_in.FragPos) <= radius ? g_DiffuseTexel.rgb * lightColor * intensity : vec3(0.0);
}

vec2 ParallaxMapping(vec3 p_ViewDir) {
    vec2 parallax = p_ViewDir.xy * data_UBO.u_HeightScale * texture(u_HeightMap, g_TexCoords).r;
    return g_TexCoords - vec2(parallax.x, 1.0 - parallax.y);
}

void main() {
	g_TexCoords = data_UBO.u_TextureOffset + vec2(mod(fs_in.TexCoord.x * data_UBO.u_TextureTiling.x, 1), mod(fs_in.TexCoord.y * data_UBO.u_TextureTiling.y, 1));

    /* Apply parallax mapping */
    if (data_UBO.u_HeightScale > 0) {
        g_TexCoords = ParallaxMapping(normalize(fs_in.TangentViewPos - fs_in.TangentFragPos));
	}

    /* Apply color mask */
    g_ViewDir           = normalize(engine_UBO.ViewPos - fs_in.FragPos);
    g_DiffuseTexel      = texture(u_DiffuseMap,  g_TexCoords) * data_UBO.u_Diffuse;
    g_SpecularTexel     = texture(u_SpecularMap, g_TexCoords) * vec4(data_UBO.u_Specular, 1.0);
    if (data_UBO.u_EnableNormalMappingBool) {
        g_Normal = texture(u_NormalMap, g_TexCoords).rgb;
        g_Normal = normalize(g_Normal * 2.0 - 1.0);
        g_Normal = normalize(fs_in.TBN * g_Normal);
    }
    else {
        g_Normal = normalize(fs_in.Normal);
    }
    g_Shininess = data_UBO.u_Shininess;
    g_FragPos = fs_in.FragPos;

	vec3 lightSum = vec3(0.0);
    for (int i = 0; i < engine_Lights.lights.length(); ++i) {
        switch(engine_Lights.lights[i].type) {
            case 0: {
                float shadow = 0.0f;
                lightSum += CalcPointLight(engine_Lights.lights[i], shadow);
                break;
            }
            case 1: {
                float shadow = 0.0f;
                lightSum += CalcDirectionalLight(engine_Lights.lights[i], shadow);
                break;
            }
            case 2: {
                float shadow = 0.0f;
                lightSum += CalcSpotLight(engine_Lights.lights[i], shadow);
                break;
            }
            case 3: lightSum += CalcAmbientBoxLight(engine_Lights.lights[i], false, 0.0f);    break;
            case 4: lightSum += CalcAmbientSphereLight(engine_Lights.lights[i], false, 0.0f); break;
        }
    }

    //FRAGMENT_COLOR = vec4(0,1,0,1);
    //FRAGMENT_COLOR = texture(u_DiffuseMap,  g_TexCoords);
	FRAGMENT_COLOR = vec4(lightSum, g_DiffuseTexel.a);
}