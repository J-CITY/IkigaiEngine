#version 450
// M4-V2 phase 2, instances: one invocation per instance rejected by the previous HZB in vgInstanceCull. Re-tested at
// the current transform against the HZB built from the phase-1 depth of this frame. Survivors are appended to the
// post visible list (same per-asset segments as the main list) and to the Y group count of the asset's post
// cluster-cull dispatch (vgClusterCull in VG_PASS_POST_INSTANCES mode). Dispatched indirectly (VgPostInstanceArgs).
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 5) uniform sampler2D VgHzb;
#define VG_CULL_OCCLUSION
#include "vgCullCommon.glsl"

layout(std430, set = 0, binding = 1) readonly buffer VgCullInstances {
	VgCullInstance Items[];
} engine_VgCullInstances;
layout(std430, set = 0, binding = 2) readonly buffer VgOccludedInstances {
	uint Words[];
} engine_VgOccludedInstances;
// Four words per asset, VgAssetDispatch layout: group count over clusters, revived instances, 1, unused.
layout(std430, set = 0, binding = 3) buffer VgPostInstanceDispatch {
	uint Words[];
} engine_VgPostInstanceDispatch;
layout(std430, set = 0, binding = 4) buffer VgPostVisibleInstances {
	uint Items[];
} engine_VgPostVisibleInstances;
layout(std430, set = 0, binding = 6) buffer VgStatistics {
	uint Words[];
} engine_VgStatistics;

void main() {
	uint entry = gl_GlobalInvocationID.x;
	if (entry >= engine_VgOccludedInstances.Words[0]) {
		return;
	}
	uint index = engine_VgOccludedInstances.Words[1u + entry];
	VgCullInstance instance = engine_VgCullInstances.Items[index];
	vec3 center = vgTransformPoint(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, instance.Bounds.xyz);
	if (vgSphereOccluded(center, instance.Bounds.w * instance.Params.y)) {
		return;
	}
	uint slot = atomicAdd(engine_VgPostInstanceDispatch.Words[instance.Indices.x * 4u + 1u], 1u);
	engine_VgPostVisibleInstances.Items[instance.Indices.y + slot] = index;
	atomicAdd(engine_VgStatistics.Words[VG_STAT_INSTANCES_REVIVED], 1u);
	atomicAdd(engine_VgStatistics.Words[VG_STAT_VISIBLE_INSTANCES], 1u);
}
