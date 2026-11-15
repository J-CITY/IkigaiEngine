#version 450
// Generate mutually exclusive candidate and flat dispatch arguments after all traversal workgroups finished.
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

#include "vgCullCommon.glsl"
#include "vgHierarchyQueue.glsl"

layout(std430, set = 0, binding = 1) readonly buffer VgAssetDispatch {
	uvec4 Items[];
} engine_VgAssetDispatch;
layout(std430, set = 0, binding = 2) readonly buffer VgHierarchyState {
	uvec4 Items[];
} engine_VgHierarchyState;
layout(std430, set = 0, binding = 3) writeonly buffer VgHierarchyDispatch {
	uvec4 Items[];
} engine_VgHierarchyDispatch;
layout(std430, set = 0, binding = 4) buffer VgStatistics {
	uint Words[];
} engine_VgStatistics;

void main() {
	uvec4 state = engine_VgHierarchyState.Items[VgCullView.Occlusion.w];
	uint candidateCount = state.y == 0u ? state.x : 0u;
	uint fallbackInstances = 0u;
	if (state.y != 0u) {
		fallbackInstances = engine_VgAssetDispatch.Items[VgCullView.Asset.x].y;
		atomicAdd(engine_VgStatistics.Words[VG_STAT_HIERARCHY_FALLBACKS], 1u);
		if ((state.y & VG_HIERARCHY_QUEUE_OVERFLOW) != 0u) {
			atomicAdd(engine_VgStatistics.Words[VG_STAT_HIERARCHY_QUEUE_OVERFLOW], 1u);
		}
		if ((state.y & VG_HIERARCHY_VISIT_OVERFLOW) != 0u) {
			atomicAdd(engine_VgStatistics.Words[VG_STAT_HIERARCHY_VISIT_OVERFLOW], 1u);
		}
	}
	uint candidateRows = (candidateCount + VG_HIERARCHY_DISPATCH_WIDTH - 1u) / VG_HIERARCHY_DISPATCH_WIDTH;
	uint flatGroups = (VgCullView.Asset.y + 63u) / 64u;
	engine_VgHierarchyDispatch.Items[0] = uvec4(min(candidateCount, VG_HIERARCHY_DISPATCH_WIDTH), candidateRows, 1u, 0u);
	engine_VgHierarchyDispatch.Items[1] = uvec4(flatGroups, fallbackInstances, 1u, 0u);
}
