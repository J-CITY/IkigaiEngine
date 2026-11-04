#version 450
// Virtual geometry G-buffer pass: one indirect draw per (mesh, material slot) batch, one draw command per
// visible (cluster, instance). The per-draw record index arrives through an instance-rate vertex stream
// (draw command FirstInstance = record index): instance-rate fetch honours FirstInstance on Vulkan and
// DirectX 12, while SV_InstanceID on DX12 does not include StartInstanceLocation.
layout(std140,set=0,binding=0) uniform EngineUBO {
mat4 View; mat4 Projection; vec3 ViewPos; float Time;
vec2 ViewportSize; float FPS; int FrameCount;
} engine_UBO;
layout(std140,set=0,binding=3) uniform FrameData {
mat4 InvViewProjection; mat4 PreviousViewProjection; mat4 PreviousModel;
vec4 CameraParams; vec4 FrameState;
} engine_Frame;

struct VgInstance { mat4 World; mat4 PreviousWorld; mat4 Normal; vec4 Params; };
layout(std430,set=0,binding=38) readonly buffer VgInstances { VgInstance Items[]; } engine_VgInstances;
struct VgDraw { uint Instance; uint Cluster; uint LodLevel; uint GroupId; };
layout(std430,set=0,binding=39) readonly buffer VgDraws { VgDraw Items[]; } engine_VgDraws;

layout(location=0) in vec3 Position;
layout(location=1) in uint EncodedNormal;
layout(location=2) in uint EncodedTangent;
layout(location=3) in vec2 Texcoord;
layout(location=4) in uint DrawIndex;

layout(location=0) out vec2 UV; layout(location=1) out vec3 World;
layout(location=2) out mat3 TBN; layout(location=5) out vec4 PreviousClip;
layout(location=6) flat out vec4 DebugColor;

// Octahedral decode of the .vmesh contract (docs/rendering/m2-virtual-geometry-spec.md, "Vertex encoding").
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
vec3 hashColor(uint v) {
	v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
	return vec3(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u)) / 255.0 * 0.8 + 0.2;
}
vec3 lodColor(uint level) {
	const vec3 palette[8] = vec3[8](vec3(0.1, 0.8, 0.1), vec3(0.1, 0.4, 1.0), vec3(1.0, 0.85, 0.1), vec3(1.0, 0.45, 0.05),
		vec3(0.9, 0.1, 0.1), vec3(0.8, 0.1, 0.9), vec3(0.1, 0.9, 0.9), vec3(1.0, 1.0, 1.0));
	return palette[min(level, 7u)];
}

void main() {
	VgDraw draw = engine_VgDraws.Items[DrawIndex];
	VgInstance instance = engine_VgInstances.Items[draw.Instance];
	vec4 world = instance.World * vec4(Position, 1.0);
	World = world.xyz; UV = Texcoord;
	mat3 normalMatrix = mat3(instance.Normal); // inverse-transpose of mat3(World)
	vec3 normal = decodeNormal(EncodedNormal);
	vec4 tangent = decodeTangent(EncodedTangent);
	vec3 bitangent = tangent.w * cross(normal, tangent.xyz);
	TBN = mat3(normalize(normalMatrix * tangent.xyz), normalize(normalMatrix * bitangent), normalize(normalMatrix * normal));
	PreviousClip = engine_Frame.PreviousViewProjection * instance.PreviousWorld * vec4(Position, 1.0);
	int mode = int(instance.Params.x + 0.5);
	DebugColor = vec4(0.0);
	if (mode == 1) DebugColor = vec4(hashColor(draw.Cluster * 2654435761u + draw.Instance), 1.0);
	else if (mode == 2) DebugColor = vec4(lodColor(draw.LodLevel), 1.0);
	else if (mode == 3) DebugColor = vec4(hashColor(draw.GroupId ^ (draw.LodLevel * 0x9e3779b9u)), 1.0);
	// M4-V2 occlusion phase: draw records from Params.y (uint bits) on were drawn by phase 2 (red), others by phase 1 (green).
	else if (mode == 4) DebugColor = vec4(DrawIndex >= floatBitsToUint(instance.Params.y) ? vec3(1.0, 0.1, 0.1) : vec3(0.1, 0.9, 0.1), 1.0);
	gl_Position = engine_UBO.Projection * engine_UBO.View * world;
}
