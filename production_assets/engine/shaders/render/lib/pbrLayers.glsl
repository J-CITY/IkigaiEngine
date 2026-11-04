// Included after the base PBR uniforms/samplers. Factors are linear; color maps
// use the engine's existing manual gamma decode. ORM/masks/heights are data.
#ifndef IKIGAI_PBR_LAYER_COUNT
#define IKIGAI_PBR_LAYER_COUNT 0
#endif

struct PbrSurface {
    vec3 color;
    float opacity;
    vec3 normal;
    float metallic;
    float roughness;
    float ao;
    float height;
};

// Flat members deliberately match the material reflection/UBO uploader.
#include "pbrLayerBindings.glsl"

float pbrHeightWeight(float weight, float baseHeight, float layerHeight, float influence, float width) {
    if (weight <= 0.0 || weight >= 1.0 || influence == 0.0) return weight;
    float shifted = weight + 4.0 * weight * (1.0 - weight) * influence * (layerHeight - baseHeight);
    float safeWidth = max(width, 0.0001);
    return smoothstep(0.5 - safeWidth * 0.5, 0.5 + safeWidth * 0.5, shifted);
}

vec3 pbrSafeNormal(vec3 value, vec3 fallback) {
    float magnitude = dot(value, value);
    return magnitude > 1e-10 ? value * inversesqrt(magnitude) : fallback;
}

float pbrLayerWeight(vec2 inputUV, float baseHeight, vec4 blend, vec4 height, vec4 maskUV,
    sampler2D maskMap, sampler2D heightMap, vec2 layerUV, out float layerHeight) {
    int flags = int(height.z);
    float mask = (flags & 16) != 0 ? texture(maskMap, inputUV * maskUV.xy + maskUV.zw)[int(blend.w)] : 1.0;
    if ((flags & 32) != 0) mask = 1.0 - mask;
    float weight = clamp(mask * blend.x, 0.0, 1.0);
    layerHeight = ((flags & 8) != 0 ? texture(heightMap, layerUV).r : 0.5) * height.x + height.y;
    return pbrHeightWeight(weight, baseHeight, layerHeight, blend.y, blend.z);
}

void pbrApplyCoverage(inout PbrSurface surface, vec2 inputUV, vec4 color, vec4 uvTransform,
    vec4 maskUV, vec4 blend, vec4 height, sampler2D albedoMap, sampler2D maskMap, sampler2D heightMap) {
    if (blend.x <= 0.0) return;
    vec2 uv = inputUV * uvTransform.xy + uvTransform.zw;
    float layerHeight;
    float weight = pbrLayerWeight(inputUV, surface.height, blend, height, maskUV, maskMap, heightMap, uv, layerHeight);
    float opacity = color.a * (((int(height.z) & 1) != 0) ? texture(albedoMap, uv).a : 1.0);
    surface.opacity = mix(surface.opacity, opacity, weight);
    surface.height = mix(surface.height, layerHeight, weight);
}

#ifndef IKIGAI_PBR_COVERAGE_ONLY
void pbrApplySurface(inout PbrSurface surface, vec2 inputUV, vec4 color, vec4 factors,
    vec4 uvTransform, vec4 maskUV, vec4 blend, vec4 height, sampler2D albedoMap,
    sampler2D normalMap, sampler2D ormMap, sampler2D maskMap, sampler2D heightMap) {
    if (blend.x <= 0.0) return;
    vec2 uv = inputUV * uvTransform.xy + uvTransform.zw;
    float layerHeight;
    float weight = pbrLayerWeight(inputUV, surface.height, blend, height, maskUV, maskMap, heightMap, uv, layerHeight);
    int flags = int(height.z);
    vec4 albedo = (flags & 1) != 0 ? texture(albedoMap, uv) : vec4(1.0);
    vec3 orm = (flags & 4) != 0 ? texture(ormMap, uv).rgb : vec3(1.0);
    vec3 layerNormal = vec3(0.0, 0.0, 1.0);
    if ((flags & 2) != 0) {
        layerNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
        layerNormal.xy *= factors.w;
        layerNormal = pbrSafeNormal(layerNormal, vec3(0.0, 0.0, 1.0));
    }
    if ((flags & 64) != 0) {
        // RNM detail composition; interpolate strength separately from coverage.
        vec3 base = surface.normal + vec3(0.0, 0.0, 1.0);
        vec3 detail = layerNormal * vec3(-1.0, -1.0, 1.0);
        layerNormal = pbrSafeNormal(base * dot(base, detail) - detail * base.z, surface.normal);
    }
    surface.color = mix(surface.color, pow(max(albedo.rgb, vec3(0.0)), vec3(2.2)) * color.rgb, weight);
    surface.opacity = mix(surface.opacity, albedo.a * color.a, weight);
    surface.normal = pbrSafeNormal(mix(surface.normal, layerNormal, weight), surface.normal);
    surface.metallic = mix(surface.metallic, orm.b * factors.x, weight);
    surface.roughness = mix(surface.roughness, orm.g * factors.y, weight);
    surface.ao = mix(surface.ao, orm.r * factors.z, weight);
    surface.height = mix(surface.height, layerHeight, weight);
}
#endif

PbrSurface pbrBaseCoverage(vec2 inputUV) {
    vec2 uv = inputUV * data_UBO.u_TextureTiling + data_UBO.u_TextureOffset;
    PbrSurface surface;
    surface.color = vec3(1.0);
    surface.opacity = texture(u_AlbedoMap, uv).a * data_UBO.u_Albedo.a;
    surface.normal = vec3(0.0, 0.0, 1.0);
    surface.metallic = 0.0;
    surface.roughness = 1.0;
    surface.ao = 1.0;
    surface.height = (pbr_Layers.Base.x > 0.5 ? texture(u_HeightMap, uv).r : 0.5) * pbr_Layers.Alpha.y + pbr_Layers.Alpha.z;
    return surface;
}

PbrSurface EvaluatePbrSurface(vec2 inputUV) {
    PbrSurface surface = pbrBaseCoverage(inputUV);
#ifndef IKIGAI_PBR_COVERAGE_ONLY
    vec2 uv = inputUV * data_UBO.u_TextureTiling + data_UBO.u_TextureOffset;
    surface.color = pow(max(texture(u_AlbedoMap, uv).rgb, vec3(0.0)), vec3(2.2)) * data_UBO.u_Albedo.rgb;
    surface.metallic = (pbr_Layers.Base.y > 0.5 ? texture(u_MetallicMap, uv).b : texture(u_MetallicMap, uv).r) * data_UBO.u_Metallic;
    surface.roughness = (pbr_Layers.Base.y > 0.5 ? texture(u_RoughnessMap, uv).g : texture(u_RoughnessMap, uv).r) * data_UBO.u_Roughness;
    surface.ao = texture(u_AmbientOcclusionMap, uv).r * pbr_Layers.Base.z;
    if (data_UBO.u_EnableNormalMapping) {
        vec3 baseNormal = texture(u_NormalMap, uv).xyz * 2.0 - 1.0;
        baseNormal.xy *= pbr_Layers.Base.w;
        surface.normal = pbrSafeNormal(baseNormal, surface.normal);
    }
#endif
    // Unrolled by compile-time layer count; the zero-layer variant declares no
    // additional samplers and executes no layer texture fetches.
#include "pbrLayerEvaluate.glsl"
    surface.metallic = clamp(surface.metallic, 0.0, 1.0);
    surface.roughness = clamp(surface.roughness, 0.04, 1.0);
    surface.ao = clamp(surface.ao, 0.0, 1.0);
    surface.opacity = clamp(surface.opacity, 0.0, 1.0);
    return surface;
}

float EvaluatePbrCoverage(vec2 inputUV) { return EvaluatePbrSurface(inputUV).opacity; }
bool PbrDiscard(float opacity) {
    int mode = int(pbr_Layers.Alpha.w);
    return mode == 1 ? opacity < pbr_Layers.Alpha.x : (mode == 2 && opacity < 0.001);
}
