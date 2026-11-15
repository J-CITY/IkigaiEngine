layout(local_size_x = VG_CLUSTER_GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 10) uniform sampler2D VgHzb;
#define VG_CULL_OCCLUSION
#include "vgCullCommon.glsl"
#ifdef VG_FLAT_BATCHED
#include "vgFlatSelection.glsl"
#endif

layout(std430, set = 0, binding = 1) readonly buffer VgCullInstances {
	VgCullInstance Items[];
} engine_VgCullInstances;
#ifdef VG_HIERARCHY_CANDIDATES
#include "vgHierarchyQueue.glsl"

layout(std430, set = 0, binding = 14) readonly buffer VgHierarchyCandidates {
	uvec4 Items[];
} engine_VgHierarchyCandidates;
layout(std430, set = 0, binding = 15) readonly buffer VgHierarchyClusters {
	uint Items[];
} engine_VgHierarchyClusters;
layout(std430, set = 0, binding = 16) readonly buffer VgHierarchyState {
	uvec4 Items[];
} engine_VgHierarchyState;
#else
layout(std430, set = 0, binding = 2) readonly buffer VgVisibleInstances {
	uint Items[];
} engine_VgVisibleInstances;
#endif
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
	uint assetIndex = VgCullView.Asset.x;
	uint clusterBase = 0u;
	uint occludedBase = VgCullView.Pass.z;
	uint occludedCapacity = VgCullView.Pass.w;
#ifdef VG_HIERARCHY_CANDIDATES
	// All lanes take the same branch before the first workgroup barrier.
	uint candidateIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * VG_HIERARCHY_DISPATCH_WIDTH;
	if (candidateIndex >= engine_VgHierarchyState.Items[VgCullView.Occlusion.w].x) {
		return;
	}
	uvec4 candidate = engine_VgHierarchyCandidates.Items[candidateIndex];
	uint instanceIndex = candidate.x;
	uint clusterIndex = 0u;
	bool testCluster = local < candidate.z;
	if (testCluster) {
		clusterIndex = engine_VgHierarchyClusters.Items[candidate.y + local];
	}
#elif defined(VG_FLAT_BATCHED)
	uint groupInAsset;
	if (!vgFlatWorkgroup(0u, assetIndex, groupInAsset)) {
		return;
	}
	VgFlatAsset asset = engine_VgFlatAssets.Items[assetIndex];
	uint groupsPerInstance = (asset.Range.y + VG_CLUSTER_GROUP_SIZE - 1u) / VG_CLUSTER_GROUP_SIZE;
	uint instanceIndex = engine_VgVisibleInstances.Items[asset.Range.z + groupInAsset / groupsPerInstance];
	uint clusterIndex = (groupInAsset % groupsPerInstance) * VG_CLUSTER_GROUP_SIZE + local;
	bool testCluster = clusterIndex < asset.Range.y;
	clusterBase = asset.Range.x;
	occludedBase = asset.Range.w;
	occludedCapacity = asset.Limits.x;
#else
	uint instanceIndex = engine_VgVisibleInstances.Items[VgCullView.Asset.z + gl_WorkGroupID.y];
	uint clusterIndex = gl_WorkGroupID.x * VG_CLUSTER_GROUP_SIZE + local;
	bool testCluster = clusterIndex < VgCullView.Asset.y;
#endif
	for (uint slotIndex = local; slotIndex < VG_LOCAL_SLOTS; slotIndex += VG_CLUSTER_GROUP_SIZE) {
		sSlotCount[slotIndex] = 0u;
		sSlotBase[slotIndex] = 0u;
	}
	for (uint stat = local; stat < VG_STAT_WORDS; stat += VG_CLUSTER_GROUP_SIZE) {
		sStatistics[stat] = 0u;
	}
	barrier();

	uint mode = VgCullView.Pass.x;
	uint segmentOffset = VgCullView.Occlusion.y;
	VgCullInstance instance = engine_VgCullInstances.Items[instanceIndex];
	bool emit = false;
	uint slot = 0u;
	uint batch = 0u;
	uint localOffset = 0u;
	VgCluster cluster;
	if (testCluster) {
#ifdef VG_HIERARCHY_CANDIDATES
		atomicAdd(sStatistics[VG_STAT_HIERARCHY_LEAF_TESTS], 1u);
#endif
		cluster = engine_VgClusters.Items[clusterBase + clusterIndex];
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
					uint entry = atomicAdd(engine_VgOccludedClusterCounts.Items[assetIndex], 1u);
					if (entry < occludedCapacity) {
						engine_VgOccludedClusters.Items[occludedBase + entry] = uvec2(instanceIndex, clusterIndex);
						if (entry % 64u == 0u) {
							atomicAdd(engine_VgPostClusterArgs.Words[assetIndex * 4u], 1u);
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
	for (uint slotIndex = local; slotIndex < min(VG_LOCAL_SLOTS, instance.Draw.z); slotIndex += VG_CLUSTER_GROUP_SIZE) {
		if (sSlotCount[slotIndex] != 0u) {
			uint slotBatch = engine_VgSlotBatches.Items[instance.Indices.z + slotIndex] + segmentOffset;
			sSlotBase[slotIndex] = atomicAdd(engine_VgBatchCounts.Items[slotBatch], sSlotCount[slotIndex]);
		}
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
	for (uint stat = local; stat < VG_STAT_WORDS; stat += VG_CLUSTER_GROUP_SIZE) {
		if (sStatistics[stat] != 0u) {
			atomicAdd(engine_VgStatistics.Words[stat], sStatistics[stat]);
		}
	}
}
