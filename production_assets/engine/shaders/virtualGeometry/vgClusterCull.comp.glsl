#version 450
// GPU virtual geometry selection, pass 2 (M4-V1): one invocation per (visible instance, cluster) of one asset.
// Dispatched indirectly: X covers the asset clusters, Y is the number of visible instances appended by
// vgInstanceCull. Every invocation runs the same flat test as the CPU (SelectLodCut + CullClusters): LOD cut,
// frustum test of the cluster bounds, normal cone. Surviving clusters append one DrawIndexedIndirectCommand and one
// draw record to the segment of their (mesh, slot, material) batch; the batch counter is the count buffer of
// drawIndexedIndirectCount. Appends are aggregated per material slot in shared memory first, so a workgroup issues
// one global atomic per slot instead of one per cluster (all invocations of a group share the instance).
//
// M4-V2 two-phase occlusion, selected by VgCullView.Pass.x:
//   VG_PASS_MAIN: phase 1. A cluster of an instance with history is also tested at the previous transform against
//     the previous frame's HZB; rejected clusters go to the asset's occluded-cluster list (re-tested by
//     vgClusterCullPost) instead of being drawn. A full list draws the cluster (conservative).
//   VG_PASS_POST_INSTANCES: phase 2 for instances revived by vgInstanceCullPost: the same full test, occlusion at the
//     current transform against the current HZB; occluded clusters are dropped. Draws go to the post segments
//     (batch + VgCullView.Occlusion.y), so phase-1 and phase-2 draws never share a segment.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 10) uniform sampler2D VgHzb;
#define VG_CULL_OCCLUSION
#include "vgCullCommon.glsl"

layout(std430, set = 0, binding = 1) readonly buffer VgCullInstances {
	VgCullInstance Items[];
} engine_VgCullInstances;
layout(std430, set = 0, binding = 2) readonly buffer VgVisibleInstances {
	uint Items[];
} engine_VgVisibleInstances;
layout(std430, set = 0, binding = 3) readonly buffer VgClusters {
	VgCluster Items[];
} engine_VgClusters;
// Batch index of every (instance, material slot): Indices.z + slot.
layout(std430, set = 0, binding = 4) readonly buffer VgSlotBatches {
	uint Items[];
} engine_VgSlotBatches;
// Per segment: x first draw of the segment, y segment capacity (post segments follow the phase-1 ones).
layout(std430, set = 0, binding = 5) readonly buffer VgBatches {
	uvec4 Items[];
} engine_VgBatches;
layout(std430, set = 0, binding = 6) buffer VgBatchCounts {
	uint Items[];
} engine_VgBatchCounts;
// DrawIndexedIndirectCommand, five words per draw.
layout(std430, set = 0, binding = 7) buffer VgDrawCommands {
	uint Words[];
} engine_VgDrawCommands;
// VgDraw of vgGBuffer.vert.glsl: instance, cluster, LOD level, group.
layout(std430, set = 0, binding = 8) buffer VgDrawRecords {
	uvec4 Items[];
} engine_VgDrawRecords;
layout(std430, set = 0, binding = 9) buffer VgStatistics {
	uint Words[];
} engine_VgStatistics;
// (cull instance, cluster) pairs rejected by phase 1; per-asset segments [Pass.z, Pass.z + Pass.w).
layout(std430, set = 0, binding = 11) buffer VgOccludedClusters {
	uvec2 Items[];
} engine_VgOccludedClusters;
// Appends per asset (may exceed the capacity: entries past it were drawn in phase 1).
layout(std430, set = 0, binding = 12) buffer VgOccludedClusterCounts {
	uint Items[];
} engine_VgOccludedClusterCounts;
// DispatchIndirectCommand of vgClusterCullPost per asset (four words).
layout(std430, set = 0, binding = 13) buffer VgPostClusterArgs {
	uint Words[];
} engine_VgPostClusterArgs;

const uint VG_LOCAL_SLOTS = 32u;
shared uint sSlotCount[VG_LOCAL_SLOTS];
shared uint sSlotBase[VG_LOCAL_SLOTS];
shared uint sStatistics[VG_STAT_WORDS];

void main() {
	uint local = gl_LocalInvocationIndex;
	if (local < VG_LOCAL_SLOTS) {
		sSlotCount[local] = 0u;
		sSlotBase[local] = 0u;
	}
	if (local < VG_STAT_WORDS) {
		sStatistics[local] = 0u;
	}
	barrier();

	uint mode = VgCullView.Pass.x;
	uint segmentOffset = VgCullView.Occlusion.y;
	uint instanceIndex = engine_VgVisibleInstances.Items[VgCullView.Asset.z + gl_WorkGroupID.y];
	VgCullInstance instance = engine_VgCullInstances.Items[instanceIndex];
	uint clusterIndex = gl_WorkGroupID.x * 64u + local;
	bool emit = false;
	uint slot = 0u;
	uint batch = 0u;
	uint localOffset = 0u;
	VgCluster cluster;
	if (clusterIndex < VgCullView.Asset.y) {
		cluster = engine_VgClusters.Items[clusterIndex];
		if (vgInLodCut(cluster, instance)) {
			uint triangles = cluster.IndexCount / 3u;
			atomicAdd(sStatistics[VG_STAT_LOD_CLUSTERS], 1u);
			atomicAdd(sStatistics[VG_STAT_LOD_TRIANGLES], triangles);
			vec3 center = vgTransformPoint(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, cluster.Bounds.xyz);
			precise float radius = cluster.Bounds.w * instance.Params.y;
			bool culled = false;
			if (VgCullView.Flags.x != 0u && !vgSphereVisible(center, radius)) {
				atomicAdd(sStatistics[VG_STAT_FRUSTUM_CULLED], 1u);
				culled = true;
			}
			else if ((instance.Indices.w & VG_INSTANCE_CONE_CULLING) != 0u && cluster.Cone.w < 1.0) {
				// meshoptimizer convention (CullClusters): back-facing for every viewer inside the cone when
				// dot(center - camera, axis) >= cutoff * |center - camera| + radius.
				vec3 axis = vgTransformDirection(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, cluster.Cone.xyz);
				float axisLength = vgLength(axis);
				if (axisLength > 0.0) {
					precise float inverseLength = 1.0 / axisLength;
					precise vec3 unitAxis = axis * inverseLength;
					precise vec3 toCenter = center - VgCullView.Camera.xyz;
					precise float limit = cluster.Cone.w * vgLength(toCenter) + radius;
					if (vgDot(toCenter, unitAxis) >= limit) {
						atomicAdd(sStatistics[VG_STAT_CONE_CULLED], 1u);
						culled = true;
					}
				}
			}
			if (!culled && mode == VG_PASS_MAIN && (instance.Indices.w & VG_INSTANCE_OCCLUSION_HISTORY) != 0u) {
				vec3 previousCenter
					= vgTransformPoint(instance.PreviousRow0, instance.PreviousRow1, instance.PreviousRow2, cluster.Bounds.xyz);
				if (vgSphereOccluded(previousCenter, cluster.Bounds.w * instance.PreviousParams.x)) {
					uint entry = atomicAdd(engine_VgOccludedClusterCounts.Items[VgCullView.Asset.x], 1u);
					if (entry < VgCullView.Pass.w) {
						engine_VgOccludedClusters.Items[VgCullView.Pass.z + entry] = uvec2(instanceIndex, clusterIndex);
						if (entry % 64u == 0u) {
							atomicAdd(engine_VgPostClusterArgs.Words[VgCullView.Asset.x * 4u], 1u);
						}
						atomicAdd(sStatistics[VG_STAT_CLUSTERS_OCCLUDED_MAIN], 1u);
						culled = true;
					}
					else {
						// No room to re-test it in phase 2: draw it now.
						atomicAdd(sStatistics[VG_STAT_OCCLUDED_LIST_OVERFLOW], 1u);
					}
				}
			}
			else if (!culled && mode == VG_PASS_POST_INSTANCES && vgSphereOccluded(center, radius)) {
				atomicAdd(sStatistics[VG_STAT_CLUSTERS_OCCLUDED_POST], 1u);
				culled = true;
			}
			if (!culled) {
				emit = true;
				slot = cluster.MaterialSlot;
				batch = engine_VgSlotBatches.Items[instance.Indices.z + slot] + segmentOffset;
				if (slot < VG_LOCAL_SLOTS) {
					localOffset = atomicAdd(sSlotCount[slot], 1u);
				}
				else {
					localOffset = atomicAdd(engine_VgBatchCounts.Items[batch], 1u);
				}
			}
		}
	}
	barrier();
	if (local < VG_LOCAL_SLOTS && local < instance.Draw.z && sSlotCount[local] != 0u) {
		uint slotBatch = engine_VgSlotBatches.Items[instance.Indices.z + local] + segmentOffset;
		sSlotBase[local] = atomicAdd(engine_VgBatchCounts.Items[slotBatch], sSlotCount[local]);
	}
	barrier();
	if (emit) {
		uint drawInSegment = slot < VG_LOCAL_SLOTS ? sSlotBase[slot] + localOffset : localOffset;
		uvec4 segment = engine_VgBatches.Items[batch];
		if (drawInSegment < segment.y) {
			uint draw = segment.x + drawInSegment;
			uint word = draw * 5u;
			engine_VgDrawCommands.Words[word + 0u] = cluster.IndexCount;
			engine_VgDrawCommands.Words[word + 1u] = 1u;
			engine_VgDrawCommands.Words[word + 2u] = cluster.IndexOffset;
			engine_VgDrawCommands.Words[word + 3u] = 0u;
			// FirstInstance = global draw index: the instance-rate stream of vgGBuffer.vert.glsl turns it into the record index.
			engine_VgDrawCommands.Words[word + 4u] = draw;
			engine_VgDrawRecords.Items[draw] = uvec4(instance.Draw.x, clusterIndex, cluster.LodLevel, cluster.GroupId);
			atomicAdd(sStatistics[VG_STAT_VISIBLE_CLUSTERS], 1u);
			atomicAdd(sStatistics[VG_STAT_VISIBLE_TRIANGLES], cluster.IndexCount / 3u);
			atomicAdd(sStatistics[VG_STAT_LOD_HISTOGRAM + min(cluster.LodLevel, 7u)], 1u);
			if (mode == VG_PASS_POST_INSTANCES) {
				atomicAdd(sStatistics[VG_STAT_CLUSTERS_POST_INSTANCES], 1u);
				atomicAdd(sStatistics[VG_STAT_POST_TRIANGLES], cluster.IndexCount / 3u);
			}
		}
		else {
			// Segment full: the draw is dropped (counted). The capacity bound is proven per asset at load time,
			// so this stays zero unless the cut is not a valid cut (float disagreement or a broken DAG).
			atomicAdd(sStatistics[VG_STAT_OVERFLOW], 1u);
		}
	}
	barrier();
	if (local < VG_STAT_WORDS && sStatistics[local] != 0u) {
		atomicAdd(engine_VgStatistics.Words[local], sStatistics[local]);
	}
}
