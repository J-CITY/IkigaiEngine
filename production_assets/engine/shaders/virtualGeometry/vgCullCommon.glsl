// Shared declarations of the GPU virtual geometry selection (M4-V1): vgInstanceCull and vgClusterCull.
// The math mirrors src/engine/virtualGeometryModule/runtime/virtualGeometrySelection.cpp operation by operation
// (row-major 3x4 transforms, left-to-right sums, explicit sqrt) and is marked precise so the compiler does not
// contract or reorder it: the CPU selection is the reference the GPU result is compared with.

// One cluster instance of the frame (CPU layout: VirtualGeometryGpuSelection::CullInstanceGpu, 176 bytes).
struct VgCullInstance {
	vec4 WorldRow0; // current world matrix rows (culling)
	vec4 WorldRow1;
	vec4 WorldRow2;
	vec4 LodRow0; // world matrix used for the LOD test (frozen with FreezeLod)
	vec4 LodRow1;
	vec4 LodRow2;
	vec4 Bounds;    // asset bounding sphere in object space (radius <= 0: never culled)
	vec4 LodCamera; // xyz camera position of the LOD view, w projection scale (pixels per unit at unit distance)
	vec4 Params;    // x threshold (pixels), y max axis scale of World, z max axis scale of LodWorld, w LOD zNear
	uvec4 Indices;  // x asset dispatch index, y visible-list segment start, z slot->batch table start, w flags
	uvec4 Draw;     // x InstanceGpu index read by vgGBuffer.vert, y cluster count, z material slot count, w reserved
	vec4 PreviousRow0; // previous-frame world matrix rows (phase-1 occlusion against the previous HZB, M4-V2)
	vec4 PreviousRow1;
	vec4 PreviousRow2;
	vec4 PreviousParams; // x max axis scale of PreviousWorld, yzw reserved
};
const uint VG_INSTANCE_CONE_CULLING = 1u;
const uint VG_INSTANCE_LOD_ORTHOGRAPHIC = 2u;
// The instance was drawn in the previous frame at PreviousWorld: phase 1 may test it against the previous HZB.
const uint VG_INSTANCE_OCCLUSION_HISTORY = 4u;

// Pass modes (VgCullView.Pass.x).
const uint VG_PASS_MAIN = 0u;           // phase 1: previous HZB, previous transforms
const uint VG_PASS_POST_INSTANCES = 1u; // phase 2: instances rejected by phase 1, current HZB, current transforms

// VMeshCluster (vmeshFormat.h, 96 bytes), read directly from the asset cluster buffer.
struct VgCluster {
	uint IndexOffset;
	uint IndexCount;
	uint MaterialSlot;
	uint GroupId;
	uint LodLevel;
	uint Flags;
	float Error;
	float ParentError;
	vec4 Bounds;
	vec4 LodBounds;
	vec4 ParentLodBounds;
	vec4 Cone; // xyz axis, w cutoff
};

layout(std140, set = 0, binding = 0) uniform VgCullViewBlock {
	vec4 Planes[6];     // world-space frustum planes (RENDER::Frustum order)
	vec4 Camera;        // xyz current camera position
	uvec4 Flags;        // x frustum culling, y instance count, z unused, w unused
	uvec4 Asset;        // x asset dispatch index, y cluster count, z visible-list segment start, w unused
	uvec4 Pass;         // x pass mode, y asset count, z occluded-cluster list base of the asset, w its capacity
	uvec4 Occlusion;    // x HZB test enabled, y first post segment (draw segment offset of this pass), z debug flags, w unused
	vec4 OcclusionViewProjection[4]; // rows of the (jittered) view-projection the HZB of this pass was rendered with
} VgCullView;
// Negative controls of the "no false holes" test (deliberately non-conservative, debug only).
const uint VG_OCCLUSION_DEBUG_SKIP_POST = 1u;   // phase 2 re-tests nothing: phase-1 errors stay as holes
const uint VG_OCCLUSION_DEBUG_UNSAFE_MIP = 2u;  // one mip finer and one texel only: the HZB footprint misses part of the rect

// Statistics words (VirtualGeometryGpuSelection::StatisticsWord).
const uint VG_STAT_VISIBLE_INSTANCES = 0u;
const uint VG_STAT_INSTANCES_CULLED = 1u;
const uint VG_STAT_LOD_CLUSTERS = 2u;
const uint VG_STAT_LOD_TRIANGLES = 3u;
const uint VG_STAT_VISIBLE_CLUSTERS = 4u;
const uint VG_STAT_VISIBLE_TRIANGLES = 5u;
const uint VG_STAT_FRUSTUM_CULLED = 6u;
const uint VG_STAT_CONE_CULLED = 7u;
const uint VG_STAT_OVERFLOW = 8u;
const uint VG_STAT_LOD_HISTOGRAM = 9u;
// Two-phase occlusion (M4-V2).
const uint VG_STAT_INSTANCES_OCCLUDED_MAIN = 17u;  // instances rejected by the previous HZB
const uint VG_STAT_INSTANCES_REVIVED = 18u;        // ... of which phase 2 found visible against the current HZB
const uint VG_STAT_CLUSTERS_OCCLUDED_MAIN = 19u;   // clusters past LOD/frustum/cone rejected by the previous HZB
const uint VG_STAT_CLUSTERS_REVIVED = 20u;         // ... of which phase 2 drew
const uint VG_STAT_CLUSTERS_POST_INSTANCES = 21u;  // clusters drawn by phase 2 for revived instances
const uint VG_STAT_CLUSTERS_OCCLUDED_POST = 22u;   // clusters rejected by the current HZB (both phase-2 passes)
const uint VG_STAT_POST_TRIANGLES = 23u;           // triangles drawn by phase 2
const uint VG_STAT_OCCLUDED_LIST_OVERFLOW = 24u;   // list full: the cluster was drawn in phase 1 instead
const uint VG_STAT_WORDS = 25u;

// Matrix4f * Vector3f of the math library: x*m0 + y*m1 + z*m2 + m3 per row.
vec3 vgTransformPoint(vec4 row0, vec4 row1, vec4 row2, vec3 p) {
	precise float x = p.x * row0.x + p.y * row0.y + p.z * row0.z + row0.w;
	precise float y = p.x * row1.x + p.y * row1.y + p.z * row1.z + row1.w;
	precise float z = p.x * row2.x + p.y * row2.y + p.z * row2.z + row2.w;
	return vec3(x, y, z);
}

vec3 vgTransformDirection(vec4 row0, vec4 row1, vec4 row2, vec3 d) {
	precise float x = row0.x * d.x + row0.y * d.y + row0.z * d.z;
	precise float y = row1.x * d.x + row1.y * d.y + row1.z * d.z;
	precise float z = row2.x * d.x + row2.y * d.y + row2.z * d.z;
	return vec3(x, y, z);
}

float vgLength(vec3 v) {
	precise float squared = v.x * v.x + v.y * v.y + v.z * v.z;
	return sqrt(squared);
}

float vgDot(vec3 a, vec3 b) {
	precise float result = a.x * b.x + a.y * b.y + a.z * b.z;
	return result;
}

// RENDER::Frustum::sphereInFrustum.
bool vgSphereVisible(vec3 center, float radius) {
	for (int i = 0; i < 6; ++i) {
		vec4 plane = VgCullView.Planes[i];
		precise float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
		if (distance <= -radius) {
			return false;
		}
	}
	return true;
}

// ProjectedErrorPixels with the LOD view of the instance. Root errors (FLT_MAX) project to +inf.
float vgProjectedError(float error, vec4 sphere, VgCullInstance instance) {
	if (error >= 3.402823466e+38) {
		return uintBitsToFloat(0x7f800000u);
	}
	precise float worldError = error * instance.Params.z;
	if ((instance.Indices.w & VG_INSTANCE_LOD_ORTHOGRAPHIC) != 0u) {
		precise float orthographic = worldError * instance.LodCamera.w;
		return orthographic;
	}
	vec3 center = vgTransformPoint(instance.LodRow0, instance.LodRow1, instance.LodRow2, sphere.xyz);
	precise float distance = vgLength(center - instance.LodCamera.xyz) - sphere.w * instance.Params.z;
	precise float scaled = worldError * instance.LodCamera.w;
	precise float pixels = scaled / max(distance, instance.Params.w);
	return pixels;
}

// IsClusterInLodCut: proj(Error, LodBounds) <= threshold < proj(ParentError, ParentLodBounds), parent first.
bool vgInLodCut(VgCluster cluster, VgCullInstance instance) {
	float threshold = instance.Params.x;
	if (!(vgProjectedError(cluster.ParentError, cluster.ParentLodBounds, instance) > threshold)) {
		return false;
	}
	return vgProjectedError(cluster.Error, cluster.LodBounds, instance) <= threshold;
}

#ifdef VG_CULL_OCCLUSION
#include "../gpuDriven/hzb/hzbOcclusion.glsl"

bool vgSphereOccluded(vec3 center, float radius) {
	return VgCullView.Occlusion.x != 0u && hzbSphereOccluded(VgHzb, VgCullView.OcclusionViewProjection, center, radius,
		(VgCullView.Occlusion.z & VG_OCCLUSION_DEBUG_UNSAFE_MIP) != 0u);
}
#endif
