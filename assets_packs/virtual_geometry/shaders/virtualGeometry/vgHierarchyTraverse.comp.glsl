#version 450
// Groups of each visible instance route to disjoint subtrees. A short cooperative fan-out then assigns
// disjoint preorder ranges to lanes, which walk by escape index. The walk writes scratch leaf ranges only.
// The next pass commits or discards every range of this asset and phase together.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "vgCullCommon.glsl"
#include "vgHierarchyQueue.glsl"

struct VgHierarchyNode {
	vec4 Bounds;
	vec4 LodCenters;
	float MinError;
	float MaxParentError;
	uint FirstCluster;
	uint ClusterCount;
	uint Escape;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

layout(std430, set = 0, binding = 1) readonly buffer VgCullInstances {
	VgCullInstance Items[];
} engine_VgCullInstances;
layout(std430, set = 0, binding = 2) readonly buffer VgVisibleInstances {
	uint Items[];
} engine_VgVisibleInstances;
layout(std430, set = 0, binding = 3) readonly buffer VgHierarchyNodes {
	VgHierarchyNode Items[];
} engine_VgHierarchyNodes;
layout(std430, set = 0, binding = 4) buffer VgHierarchyState {
	uvec4 Items[];
} engine_VgHierarchyState;
// Each range: cull instance index, first ID in the hierarchy permutation, number of IDs, spare.
layout(std430, set = 0, binding = 5) writeonly buffer VgHierarchyCandidates {
	uvec4 Items[];
} engine_VgHierarchyCandidates;
layout(std430, set = 0, binding = 6) buffer VgStatistics {
	uint Words[];
} engine_VgStatistics;

const uint VG_NO_NODE = 0xffffffffu;
const uint VG_HIERARCHY_MAX_TRAVERSAL_GROUPS = 64u;
const uint VG_HIERARCHY_NODES_PER_TRAVERSAL_GROUP = 128u;
const float VG_NODE_ROUNDING_MARGIN = 1e-5;
shared uint sFrontier[2][64];
shared uint sSubtreeNodeCount;
shared uint sVisited;

vec3 vgTransformMagnitude(vec4 row0, vec4 row1, vec4 row2, vec4 bounds) {
	vec3 point = abs(bounds.xyz) + vec3(bounds.w);
	return vec3(vgDot(abs(row0.xyz), point), vgDot(abs(row1.xyz), point), vgDot(abs(row2.xyz), point))
		+ abs(vec3(row0.w, row1.w, row2.w));
}

bool vgRejectNodeFrustum(vec4 bounds, VgCullInstance instance) {
	if (VgCullView.Flags.x == 0u || !(instance.Params.y >= 0.0)) {
		return false;
	}
	vec3 center = vgTransformPoint(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, bounds.xyz);
	vec3 magnitude = vgTransformMagnitude(instance.WorldRow0, instance.WorldRow1, instance.WorldRow2, bounds);
	for (int i = 0; i < 6; ++i) {
		vec4 plane = VgCullView.Planes[i];
		// A leaf of radius r has at most (R - r) of center displacement inside node radius R. Its
		// plane reach is bounded by (R - r) * |nM| + r * maxScale, including shear and nonunit planes.
		// Scale before taking a length. A tiny matrix coefficient can have substantial reach at large R.
		precise vec3 scaledNormal = plane.x * (instance.WorldRow0.xyz * bounds.w) + plane.y * (instance.WorldRow1.xyz * bounds.w)
			+ plane.z * (instance.WorldRow2.xyz * bounds.w);
		float reach = max(vgLength(scaledNormal), bounds.w * instance.Params.y);
		float roundingFloor = (1.0 + abs(plane.x) + abs(plane.y) + abs(plane.z)) * 1e-18;
		float roundoff = (vgDot(abs(plane.xyz), magnitude) + abs(plane.w) + reach) * VG_NODE_ROUNDING_MARGIN + roundingFloor;
		precise float distance = vgDot(plane.xyz, center) + plane.w;
		if (distance < -(reach + roundoff)) {
			return true;
		}
	}
	return false;
}

bool vgRejectNodeLod(VgHierarchyNode node, VgCullInstance instance) {
	bool orthographic = (instance.Indices.w & VG_INSTANCE_LOD_ORTHOGRAPHIC) != 0u;
	float threshold = instance.Params.x;
	if (!(instance.Params.z >= 0.0 && instance.LodCamera.w >= 0.0 && (orthographic || instance.Params.w > 0.0))
		|| isnan(threshold) || isinf(threshold)) {
		return false;
	}
	// The exact parent denominator is at least ZNear, which makes this an upper bound.
	if (node.MaxParentError < 3.402823466e+38) {
		precise float numerator = (node.MaxParentError * instance.Params.z) * instance.LodCamera.w;
		precise float parentUpper = orthographic ? numerator : numerator / instance.Params.w;
		if (parentUpper <= threshold) {
			return true;
		}
	}
	if (node.MinError > 0.0) {
		precise float numerator = (node.MinError * instance.Params.z) * instance.LodCamera.w;
		if (orthographic) {
			return numerator > threshold;
		}
		// Replacing each leaf's distance-minus-radius with the farthest possible center distance
		// lowers its projected error. The Frobenius norm bounds the expansion under shear.
		vec3 center = vgTransformPoint(instance.LodRow0, instance.LodRow1, instance.LodRow2, node.LodCenters.xyz);
		vec3 row0 = instance.LodRow0.xyz * node.LodCenters.w;
		vec3 row1 = instance.LodRow1.xyz * node.LodCenters.w;
		vec3 row2 = instance.LodRow2.xyz * node.LodCenters.w;
		float norm = sqrt(vgDot(row0, row0) + vgDot(row1, row1) + vgDot(row2, row2));
		float reach = vgLength(center - instance.LodCamera.xyz) + norm;
		vec3 magnitude = vgTransformMagnitude(instance.LodRow0, instance.LodRow1, instance.LodRow2, node.LodCenters)
			+ abs(instance.LodCamera.xyz);
		float upper = reach + (reach + magnitude.x + magnitude.y + magnitude.z) * VG_NODE_ROUNDING_MARGIN + 1e-18;
		// Large values may overflow the exact leaf's squared length, so leave those leaves to the flat predicate.
		if (upper < 1e18) {
			precise float ownLower = numerator / max(upper, instance.Params.w);
			return ownLower > threshold;
		}
	}
	return false;
}

bool vgAcquireVisit(uint stateIndex, inout uint visited) {
	uint budget = VgCullView.Flags.w;
	if (budget != 0xffffffffu) {
		uint used = atomicAdd(engine_VgHierarchyState.Items[stateIndex].z, 0u);
		while (used < budget) {
			uint previous = atomicCompSwap(engine_VgHierarchyState.Items[stateIndex].z, used, used + 1u);
			if (previous == used) {
				++visited;
				return true;
			}
			used = previous;
		}
		atomicOr(engine_VgHierarchyState.Items[stateIndex].y, VG_HIERARCHY_VISIT_OVERFLOW);
		return false;
	}
	++visited;
	return true;
}

void vgQueueLeaf(uint stateIndex, uint instanceIndex, VgHierarchyNode leaf) {
	if ((atomicAdd(engine_VgHierarchyState.Items[stateIndex].y, 0u) & VG_HIERARCHY_QUEUE_OVERFLOW) != 0u) {
		return;
	}
	// Each lane can make at most one reservation beyond capacity before it sets the sticky flag.
	// Capacity + 65535 instances * 64 groups * 64 lanes is below uint32 overflow.
	uint entry = atomicAdd(engine_VgHierarchyState.Items[stateIndex].x, 1u);
	if (entry < VgCullView.Flags.z) {
		engine_VgHierarchyCandidates.Items[entry] = uvec4(instanceIndex, leaf.FirstCluster, leaf.ClusterCount, 0u);
	} else {
		atomicOr(engine_VgHierarchyState.Items[stateIndex].y, VG_HIERARCHY_QUEUE_OVERFLOW);
	}
}

// Matches HierarchyTraversalGroupCount in virtualGeometryGpuSelection.cpp.
uint vgTraversalGroupCount(uint nodeCount) {
	uint groups = 1u;
	uint nodes = nodeCount - 1u;
	while (groups < VG_HIERARCHY_MAX_TRAVERSAL_GROUPS && nodes >= VG_HIERARCHY_NODES_PER_TRAVERSAL_GROUP) {
		groups *= 2u;
		nodes >>= 1u;
	}
	return groups;
}

uint vgRouteSubtree(uint groups, uint stateIndex, uint instanceIndex, VgCullInstance instance, inout uint visited) {
	uint index = 0u;
	uint route = gl_WorkGroupID.x;
	for (uint branchWidth = groups / 2u; branchWidth > 0u; branchWidth /= 2u) {
		if (!vgAcquireVisit(stateIndex, visited)) {
			return VG_NO_NODE;
		}
		VgHierarchyNode node = engine_VgHierarchyNodes.Items[index];
		if (vgRejectNodeFrustum(node.Bounds, instance) || vgRejectNodeLod(node, instance)) {
			return VG_NO_NODE;
		}
		if (node.Escape == index + 1u) {
			// Every group with the same prefix arrived at this early leaf. Only the zero suffix owns it.
			if (route == 0u) {
				vgQueueLeaf(stateIndex, instanceIndex, node);
			}
			return VG_NO_NODE;
		}
		++index;
		if (route >= branchWidth) {
			index = engine_VgHierarchyNodes.Items[index].Escape;
			route -= branchWidth;
		}
	}
	return index;
}

void main() {
	uint lane = gl_LocalInvocationIndex;
	uint stateIndex = VgCullView.Occlusion.w;
	uint groups = vgTraversalGroupCount(VgCullView.Asset.w);
	// Vulkan can expose a taller dispatch than the reservation counter's proof assumes. Any extra
	// group marks the entire asset and phase for flat fallback without entering the traversal.
	if (gl_WorkGroupID.y >= VG_HIERARCHY_DISPATCH_WIDTH || gl_WorkGroupID.x >= groups) {
		if (lane == 0u) {
			atomicOr(engine_VgHierarchyState.Items[stateIndex].y, VG_HIERARCHY_QUEUE_OVERFLOW);
		}
		return;
	}
	uint instanceIndex = engine_VgVisibleInstances.Items[VgCullView.Asset.z + gl_WorkGroupID.y];
	VgCullInstance instance = engine_VgCullInstances.Items[instanceIndex];
	uint visited = 0u;
	if (lane == 0u) {
		uint root = vgRouteSubtree(groups, stateIndex, instanceIndex, instance, visited);
		sFrontier[0][0] = root;
		sSubtreeNodeCount = root == VG_NO_NODE ? 0u : engine_VgHierarchyNodes.Items[root].Escape - root;
		sVisited = 0u;
	}
	barrier();
	uint nodeCount = sSubtreeNodeCount;
	if (nodeCount == 0u) {
		if (lane == 0u && visited != 0u) {
			atomicAdd(engine_VgStatistics.Words[VG_STAT_HIERARCHY_VISITED_NODES], visited);
		}
		return;
	}

	uint frontier = 0u;
	uint width = 1u;
	uint targetWidth = nodeCount > 128u ? 64u : (nodeCount > 32u ? 16u : (nodeCount > 8u ? 4u : 1u));
	while (width < targetWidth) {
		if (lane < width) {
			uint left = VG_NO_NODE;
			uint right = VG_NO_NODE;
			uint index = sFrontier[frontier][lane];
			if (index != VG_NO_NODE && vgAcquireVisit(stateIndex, visited)) {
				VgHierarchyNode node = engine_VgHierarchyNodes.Items[index];
				if (!vgRejectNodeFrustum(node.Bounds, instance) && !vgRejectNodeLod(node, instance)) {
					if (node.Escape == index + 1u) {
						vgQueueLeaf(stateIndex, instanceIndex, node);
					} else {
						left = index + 1u;
						right = engine_VgHierarchyNodes.Items[left].Escape;
					}
				}
			}
			sFrontier[1u - frontier][2u * lane] = left;
			sFrontier[1u - frontier][2u * lane + 1u] = right;
		}
		barrier();
		frontier = 1u - frontier;
		width *= 2u;
	}

	if (lane < width) {
		uint index = sFrontier[frontier][lane];
		uint end = index == VG_NO_NODE ? 0u : engine_VgHierarchyNodes.Items[index].Escape;
		while (index < end && vgAcquireVisit(stateIndex, visited)) {
			VgHierarchyNode node = engine_VgHierarchyNodes.Items[index];
			if (vgRejectNodeFrustum(node.Bounds, instance) || vgRejectNodeLod(node, instance)) {
				index = node.Escape;
				continue;
			}
			if (node.Escape == index + 1u) {
				vgQueueLeaf(stateIndex, instanceIndex, node);
			}
			++index;
		}
	}
	if (visited != 0u) {
		atomicAdd(sVisited, visited);
	}
	barrier();
	if (lane == 0u && sVisited != 0u) {
		atomicAdd(engine_VgStatistics.Words[VG_STAT_HIERARCHY_VISITED_NODES], sVisited);
	}
}
