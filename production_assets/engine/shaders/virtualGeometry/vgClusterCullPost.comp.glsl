#version 450
// M4-V2 phase 2, clusters: one invocation per entry of one asset's occluded-cluster list (clusters that passed LOD,
// frustum and cone in phase 1 but were rejected by the previous HZB). Only the occlusion test is repeated: current
// transform, current HZB (built from the phase-1 depth of this frame). Visible clusters append to the post segment
// of their batch. Invocations of a group belong to different instances, so appends use one global atomic each.
// Dispatched indirectly: X grows by one every 64 list appends in vgClusterCull.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 11) uniform sampler2D VgHzb;
#define VG_CULL_OCCLUSION
#include "vgCullCommon.glsl"

layout(std430, set = 0, binding = 1) readonly buffer VgCullInstances {
	VgCullInstance Items[];
} engine_VgCullInstances;
layout(std430, set = 0, binding = 2) readonly buffer VgOccludedClusters {
	uvec2 Items[];
} engine_VgOccludedClusters;
layout(std430, set = 0, binding = 3) readonly buffer VgOccludedClusterCounts {
	uint Items[];
} engine_VgOccludedClusterCounts;
layout(std430, set = 0, binding = 4) readonly buffer VgClusters {
	VgCluster Items[];
} engine_VgClusters;
layout(std430, set = 0, binding = 5) readonly buffer VgSlotBatches {
	uint Items[];
} engine_VgSlotBatches;
layout(std430, set = 0, binding = 6) readonly buffer VgBatches {
	uvec4 Items[];
} engine_VgBatches;
layout(std430, set = 0, binding = 7) buffer VgBatchCounts {
	uint Items[];
} engine_VgBatchCounts;
layout(std430, set = 0, binding = 8) buffer VgDrawCommands {
	uint Words[];
} engine_VgDrawCommands;
layout(std430, set = 0, binding = 9) buffer VgDrawRecords {
	uvec4 Items[];
} engine_VgDrawRecords;
layout(std430, set = 0, binding = 10) buffer VgStatistics {
	uint Words[];
} engine_VgStatistics;

void main() {
	uint entry = gl_GlobalInvocationID.x;
	uint count = min(engine_VgOccludedClusterCounts.Items[VgCullView.Asset.x], VgCullView.Pass.w);
	if (entry >= count) {
		return;
	}
	uvec2 pair = engine_VgOccludedClusters.Items[VgCullView.Pass.z + entry];
	VgCullInstance instance = engine_VgCullInstances.Items[pair.x];
	VgCluster cluster = engine_VgClusters.Items[pair.y];
	vec3 center = vgTransformPoint(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, cluster.Bounds.xyz);
	precise float radius = cluster.Bounds.w * instance.Params.y;
	if (vgSphereOccluded(center, radius)) {
		atomicAdd(engine_VgStatistics.Words[VG_STAT_CLUSTERS_OCCLUDED_POST], 1u);
		return;
	}
	uint batch = engine_VgSlotBatches.Items[instance.Indices.z + cluster.MaterialSlot] + VgCullView.Occlusion.y;
	uint drawInSegment = atomicAdd(engine_VgBatchCounts.Items[batch], 1u);
	uvec4 segment = engine_VgBatches.Items[batch];
	if (drawInSegment >= segment.y) {
		atomicAdd(engine_VgStatistics.Words[VG_STAT_OVERFLOW], 1u);
		return;
	}
	uint draw = segment.x + drawInSegment;
	uint word = draw * 5u;
	engine_VgDrawCommands.Words[word + 0u] = cluster.IndexCount;
	engine_VgDrawCommands.Words[word + 1u] = 1u;
	engine_VgDrawCommands.Words[word + 2u] = cluster.IndexOffset;
	engine_VgDrawCommands.Words[word + 3u] = 0u;
	engine_VgDrawCommands.Words[word + 4u] = draw;
	engine_VgDrawRecords.Items[draw] = uvec4(instance.Draw.x, pair.y, cluster.LodLevel, cluster.GroupId);
	atomicAdd(engine_VgStatistics.Words[VG_STAT_VISIBLE_CLUSTERS], 1u);
	atomicAdd(engine_VgStatistics.Words[VG_STAT_VISIBLE_TRIANGLES], cluster.IndexCount / 3u);
	atomicAdd(engine_VgStatistics.Words[VG_STAT_LOD_HISTOGRAM + min(cluster.LodLevel, 7u)], 1u);
	atomicAdd(engine_VgStatistics.Words[VG_STAT_CLUSTERS_REVIVED], 1u);
	atomicAdd(engine_VgStatistics.Words[VG_STAT_POST_TRIANGLES], cluster.IndexCount / 3u);
}
