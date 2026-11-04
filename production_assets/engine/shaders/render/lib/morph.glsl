layout(std430,set=0,binding=31) readonly buffer MorphDeltas {
    vec4 values[];
} engine_MorphDeltas;
layout(std140,set=0,binding=35) uniform MorphWeights {
    ivec4 Meta;
    vec4 Current[16];
    vec4 Previous[16];
} engine_MorphWeights;

float morphWeight(int target, bool previous) {
    vec4 group = previous ? engine_MorphWeights.Previous[target / 4] : engine_MorphWeights.Current[target / 4];
    return group[target % 4];
}

vec3 morphAttribute(vec3 base, int attributeIndex, bool previous) {
    int count = engine_MorphWeights.Meta.x;
    int vertexCount = engine_MorphWeights.Meta.y;
    for (int target = 0; target < count; ++target) {
        float weight = morphWeight(target, previous);
        if (weight != 0.0) {
            int index = (target * vertexCount + gl_VertexIndex) * 3 + attributeIndex;
            base += engine_MorphDeltas.values[index].xyz * weight;
        }
    }
    return base;
}

vec3 morphPosition(vec3 position, bool previous) { return morphAttribute(position, 0, previous); }
vec3 morphNormal(vec3 normal, bool previous) {
    if (engine_MorphWeights.Meta.x == 0) return normal;
    vec3 result = morphAttribute(normal, 1, previous);
    return dot(result, result) > 1e-10 ? normalize(result) : normal;
}
vec3 morphTangent(vec3 tangent) {
    if (engine_MorphWeights.Meta.x == 0) return tangent;
    vec3 result = morphAttribute(tangent, 2, false);
    return dot(result, result) > 1e-10 ? normalize(result) : tangent;
}
vec3 morphBitangent(vec3 baseNormal, vec3 baseTangent, vec3 normal, vec3 tangent, vec3 originalBitangent) {
    if (engine_MorphWeights.Meta.x == 0) return originalBitangent;
    float handedness = dot(cross(baseNormal, baseTangent), originalBitangent) < 0.0 ? -1.0 : 1.0;
    return normalize(cross(normal, tangent)) * handedness;
}
