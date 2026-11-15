#version 450
layout(std140,set=0,binding=0) uniform EngineUBO {
    mat4 View; mat4 Projection; vec3 ViewPos; float Time;
    vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
    mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
    vec4 CameraParams; vec4 FrameState;
} engine_Frame;
layout(std140,set=0,binding=4) uniform Parameters {
    vec4 u_Albedo; vec2 u_TextureTiling; vec2 u_TextureOffset;
    float u_Metallic; float u_Roughness; bool u_EnableNormalMapping;
} data_UBO;
layout(set=0,binding=8) uniform sampler2D u_AlbedoMap;
layout(set=0,binding=9) uniform sampler2D u_NormalMap;

struct VgInstance { mat4 World; mat4 PreviousWorld; mat4 Normal; vec4 Params; };
struct VgCluster {
    uint IndexOffset; uint IndexCount; uint MaterialSlot; uint GroupId;
    uint LodLevel; uint Flags; float Error; float ParentError;
    vec4 Bounds; vec4 LodBounds; vec4 ParentLodBounds; vec4 Cone;
};
layout(std430,set=0,binding=38) readonly buffer VgInstances { VgInstance Items[]; } engine_VgInstances;
layout(std430,set=0,binding=39) readonly buffer VgDraws { uvec4 Items[]; } engine_VgDraws;
layout(std430,set=0,binding=40) readonly buffer VgResolveVertices { uint Words[]; } engine_VgResolveVertices;
layout(std430,set=0,binding=41) readonly buffer VgResolveIndices { uint Items[]; } engine_VgResolveIndices;
layout(std430,set=0,binding=42) readonly buffer VgResolveClusters { VgCluster Items[]; } engine_VgResolveClusters;
layout(set=0,binding=43) uniform isampler2D engine_VgVisibility;
layout(std140,set=0,binding=44) uniform VgResolveRange { uvec4 Range; } engine_VgResolveRange;
layout(location=0) out vec4 VelocityOut;
layout(location=1) out vec4 NormalOut;
layout(location=2) out vec4 AlbedoOut;
layout(location=3) out vec4 RoughOut;

vec3 octDecode(vec2 f) {
	vec3 v = vec3(f, 1.0 - abs(f.x) - abs(f.y));
	if (v.z < 0.0) {
		vec2 s = vec2(f.x >= 0.0 ? 1.0 : -1.0, f.y >= 0.0 ? 1.0 : -1.0);
		v.xy = (1.0 - abs(f.yx)) * s;
	}
	return normalize(v);
}
vec3 decodeNormal(uint e) {
	int x = int(e << 16) >> 16;
	int y = int(e) >> 16;
	return octDecode(max(vec2(float(x), float(y)) / 32767.0, vec2(-1.0)));
}
vec4 decodeTangent(uint e) {
	int x = int(e << 17) >> 17;
	int y = int(e << 2) >> 17;
	vec3 t = octDecode(clamp(vec2(float(x), float(y)) / 16383.0, vec2(-1.0), vec2(1.0)));
	return vec4(t, (e & 0x80000000u) != 0u ? -1.0 : 1.0);
}

struct Vertex {
    vec3 Position;
    vec2 UV;
    mat3 TBN;
};
Vertex loadVertex(uint index, mat3 normalMatrix) {
    uint first = index * 7u;
    vec3 p = vec3(uintBitsToFloat(engine_VgResolveVertices.Words[first]),
        uintBitsToFloat(engine_VgResolveVertices.Words[first + 1u]), uintBitsToFloat(engine_VgResolveVertices.Words[first + 2u]));
    vec3 n = decodeNormal(engine_VgResolveVertices.Words[first + 3u]);
    vec4 t = decodeTangent(engine_VgResolveVertices.Words[first + 4u]);
    vec2 uv = vec2(uintBitsToFloat(engine_VgResolveVertices.Words[first + 5u]),
        uintBitsToFloat(engine_VgResolveVertices.Words[first + 6u]));
    vec3 b = t.w * cross(n, t.xyz);
    return Vertex(p, uv, mat3(normalize(normalMatrix * t.xyz), normalize(normalMatrix * b), normalize(normalMatrix * n)));
}

float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
vec2 pixelPosition(vec4 clip) {
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    uv.y = mix(uv.y, 1.0 - uv.y, engine_Frame.CameraParams.w);
    return uv * engine_UBO.ViewportSize;
}

void main() {
    ivec2 id = texelFetch(engine_VgVisibility, ivec2(gl_FragCoord.xy), 0).rg;
    uint first = engine_VgResolveRange.Range.x;
    if (id.x < 0 || id.y < 0 || uint(id.x) < first || uint(id.x) - first >= engine_VgResolveRange.Range.y) discard;
    uvec4 draw = engine_VgDraws.Items[id.x];
    VgCluster cluster = engine_VgResolveClusters.Items[draw.y];
    if (uint(id.y) >= cluster.IndexCount / 3u) discard;
    uint index = cluster.IndexOffset + uint(id.y) * 3u;
    VgInstance instance = engine_VgInstances.Items[draw.x];
    Vertex vertices[3];
    vec4 world[3], clip[3], previousClip[3];
    vec2 pixel[3];
    for (uint i = 0; i < 3u; ++i) {
        vertices[i] = loadVertex(engine_VgResolveIndices.Items[index + i], mat3(instance.Normal));
        vec4 local = vec4(vertices[i].Position, 1.0);
        world[i] = instance.World * local;
        clip[i] = engine_UBO.Projection * engine_UBO.View * world[i];
        previousClip[i] = engine_Frame.PreviousViewProjection * instance.PreviousWorld * local;
        pixel[i] = pixelPosition(clip[i]);
    }

    vec2 edge1 = pixel[1] - pixel[0];
    vec2 edge2 = pixel[2] - pixel[0];
    vec2 offset = gl_FragCoord.xy - pixel[0];
    float determinant = cross2(edge1, edge2);
    if (abs(determinant) < 1e-8) discard;
    float lambda1 = cross2(offset, edge2) / determinant;
    float lambda2 = cross2(edge1, offset) / determinant;
    vec3 lambda = vec3(1.0 - lambda1 - lambda2, lambda1, lambda2);
    vec3 reciprocalW = 1.0 / vec3(clip[0].w, clip[1].w, clip[2].w);
    float denominator = dot(lambda, reciprocalW);
    if (!(denominator > 0.0)) discard;
    vec3 weights = lambda * reciprocalW / denominator;

    vec2 uv = vertices[0].UV * weights.x + vertices[1].UV * weights.y + vertices[2].UV * weights.z;
    // Differentiate the same perspective quotient analytically. Neighbouring
    // fullscreen fragments can belong to different triangles or material bins.
    vec3 dx = vec3(edge1.y - edge2.y, edge2.y, -edge1.y) / determinant;
    vec3 dy = vec3(edge2.x - edge1.x, -edge2.x, edge1.x) / determinant;
    vec2 gradientX = vec2(0.0), gradientY = vec2(0.0);
    for (uint i = 0; i < 3u; ++i) {
        vec2 term = reciprocalW[i] * (vertices[i].UV - uv) / denominator;
        gradientX += dx[i] * term;
        gradientY += dy[i] * term;
    }
    gradientX *= data_UBO.u_TextureTiling;
    gradientY *= data_UBO.u_TextureTiling;
    uv = uv * data_UBO.u_TextureTiling + data_UBO.u_TextureOffset;

    vec4 albedo = textureGrad(u_AlbedoMap, uv, gradientX, gradientY) * data_UBO.u_Albedo;
    mat3 tbn = vertices[0].TBN * weights.x + vertices[1].TBN * weights.y + vertices[2].TBN * weights.z;
    vec3 normal = normalize(tbn[2]);
    if (data_UBO.u_EnableNormalMapping) normal = normalize(tbn * (textureGrad(u_NormalMap, uv, gradientX, gradientY).xyz * 2.0 - 1.0));
    vec4 worldPosition = world[0] * weights.x + world[1] * weights.y + world[2] * weights.z;
    vec4 previous = previousClip[0] * weights.x + previousClip[1] * weights.y + previousClip[2] * weights.z;
    vec2 previousUv = previous.xy / max(abs(previous.w), 1e-5) * sign(previous.w) * 0.5 + 0.5;
    previousUv.y = mix(previousUv.y, 1.0 - previousUv.y, engine_Frame.CameraParams.w);
    vec2 velocity = gl_FragCoord.xy / engine_UBO.ViewportSize - previousUv - engine_Frame.FrameState.zw;

    VelocityOut = vec4(engine_Frame.FrameState.x > 0.5 ? velocity : vec2(0.0), 0, 1);
    NormalOut = vec4(normal, 1);
    AlbedoOut = vec4(pow(max(albedo.rgb, vec3(0)), vec3(2.2)), clamp(data_UBO.u_Metallic, 0, 1));
    RoughOut = vec4(clamp(data_UBO.u_Roughness, 0.04, 1), 1, -(engine_UBO.View * worldPosition).z, 1);
}
