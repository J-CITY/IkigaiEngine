#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "virtualGeometrySelection.h"

namespace IKIGAI::VG {
// Runtime-only GPU layout. A full binary tree in preorder, rooted at zero. The left child immediately
// follows its parent and the right child follows the left subtree. Escape skips the entire subtree.
struct VirtualGeometryHierarchyNode {
	VMeshSphere Bounds = {};
	VMeshSphere LodCenters = {};
	float MinError = 0.0f;
	float MaxParentError = 0.0f;
	uint32_t FirstCluster = 0;
	uint32_t ClusterCount = 0;
	uint32_t Escape = 0;
	uint32_t Reserved[3] = {};
};
static_assert(sizeof(VirtualGeometryHierarchyNode) == 64);
static_assert(offsetof(VirtualGeometryHierarchyNode, LodCenters) == 16);
static_assert(offsetof(VirtualGeometryHierarchyNode, MinError) == 32);
static_assert(offsetof(VirtualGeometryHierarchyNode, Escape) == 48);

struct VirtualGeometryHierarchy {
	std::vector<VirtualGeometryHierarchyNode> Nodes = {};
	// A permutation of the original cluster table IDs. Each node covers a contiguous slice.
	std::vector<uint32_t> ClusterIds = {};

	size_t bytes() const;
};

struct VirtualGeometryHierarchyStatistics {
	uint32_t NodesVisited = 0;
	uint32_t LeavesVisited = 0;
	uint64_t ClustersTested = 0;
	uint32_t NodesLodRejected = 0;
	bool UsedFlatFallback = false;
};

// Builds from the validated cluster table of a resident mesh.
VirtualGeometryHierarchy BuildVirtualGeometryHierarchy(const std::vector<VMeshCluster>& clusters);

// Appends the exact LOD cut in cluster table order. The caller can cache this cut across changes of
// the culling view (FreezeLod). On visit budget exhaustion, rewinds this append and selects flat.
VirtualGeometryHierarchyStatistics SelectHierarchyLodCut(const VirtualGeometryHierarchy& hierarchy, const std::vector<VMeshCluster>& clusters,
	const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view, float thresholdPixels, std::vector<uint32_t>& selected,
	uint32_t visitBudget = std::numeric_limits<uint32_t>::max());
}
