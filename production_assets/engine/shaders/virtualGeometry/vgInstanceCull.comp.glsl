#version 450
// GPU virtual geometry selection, pass 1 (M4-V1): one invocation per cluster instance. Frustum test of the asset
// bounding sphere (scaled by the max axis scale, as IsInstanceVisible) and compaction of the visible instances into
// the per-asset segment of the visible list. The append counter is the Y group count of the asset's indirect
// cluster-cull dispatch, so no CPU round trip or separate argument pass is needed.
// M4-V2 phase 1: an instance with history is also tested at its previous transform against the previous frame's
// HZB. Rejected instances go to the occluded-instance list (count in word 0) for vgInstanceCullPost; the X group
// count of its indirect dispatch grows by one every 64 appends.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 5) uniform sampler2D VgHzb;
#define VG_CULL_OCCLUSION
#include "vgCullCommon.glsl"

layout(std430, set = 0, binding = 1) readonly buffer VgCullInstances {
	VgCullInstance Items[];
} engine_VgCullInstances;
// Four words per asset: group count over clusters, visible instances (appended here), 1, unused.
layout(std430, set = 0, binding = 2) buffer VgAssetDispatch {
	uint Words[];
} engine_VgAssetDispatch;
layout(std430, set = 0, binding = 3) buffer VgVisibleInstances {
	uint Items[];
} engine_VgVisibleInstances;
layout(std430, set = 0, binding = 4) buffer VgStatistics {
	uint Words[];
} engine_VgStatistics;
// Word 0: number of occluded instances, then their indices.
layout(std430, set = 0, binding = 6) buffer VgOccludedInstances {
	uint Words[];
} engine_VgOccludedInstances;
// DispatchIndirectCommand of vgInstanceCullPost.
layout(std430, set = 0, binding = 7) buffer VgPostInstanceArgs {
	uint Words[];
} engine_VgPostInstanceArgs;

void main() {
	uint index = gl_GlobalInvocationID.x;
	if (index >= VgCullView.Flags.y) {
		return;
	}
	VgCullInstance instance = engine_VgCullInstances.Items[index];
	bool visible = true;
	if (VgCullView.Flags.x != 0u && instance.Bounds.w > 0.0) {
		vec3 center = vgTransformPoint(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, instance.Bounds.xyz);
		precise float radius = instance.Bounds.w * instance.Params.y;
		visible = vgSphereVisible(center, radius);
	}
	if (!visible) {
		atomicAdd(engine_VgStatistics.Words[VG_STAT_INSTANCES_CULLED], 1u);
		return;
	}
	if ((instance.Indices.w & VG_INSTANCE_OCCLUSION_HISTORY) != 0u) {
		vec3 previousCenter = vgTransformPoint(instance.PreviousRow0, instance.PreviousRow1, instance.PreviousRow2, instance.Bounds.xyz);
		if (vgSphereOccluded(previousCenter, instance.Bounds.w * instance.PreviousParams.x)) {
			uint slot = atomicAdd(engine_VgOccludedInstances.Words[0], 1u);
			engine_VgOccludedInstances.Words[1u + slot] = index;
			if (slot % 64u == 0u) {
				atomicAdd(engine_VgPostInstanceArgs.Words[0], 1u);
			}
			atomicAdd(engine_VgStatistics.Words[VG_STAT_INSTANCES_OCCLUDED_MAIN], 1u);
			return;
		}
	}
	uint slot = atomicAdd(engine_VgAssetDispatch.Words[instance.Indices.x * 4u + 1u], 1u);
	engine_VgVisibleInstances.Items[instance.Indices.y + slot] = index;
	atomicAdd(engine_VgStatistics.Words[VG_STAT_VISIBLE_INSTANCES], 1u);
}
