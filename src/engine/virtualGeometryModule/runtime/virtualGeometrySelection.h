#pragma once

#include <cstdint>
#include <vector>

#include "mathModule/math.h"
#include "virtualGeometryModule/vmeshFormat.h"

namespace IKIGAI::RENDER {
class Frustum;
}

namespace IKIGAI::VG {
// Camera parameters for screen-space error projection and culling (one view, CPU selection, M2).
struct VirtualGeometryView {
	MATH::Vector3f CameraPosition = {};
	// World-space culling frustum; nullptr disables frustum culling.
	const RENDER::Frustum* Frustum = nullptr;
	// Pixels per unit of object-space error at unit view distance:
	// perspective viewportHeightPx / (2 * tan(fovY / 2)), orthographic viewportHeightPx / orthoHeight.
	// Both equal viewportHeightPx * Projection(1, 1) / 2.
	float ProjectionScale = 1.0f;
	bool Orthographic = false;
	float ZNear = 0.1f;
};

// Per-instance transform data derived from the world matrix.
struct VirtualGeometryInstanceTransform {
	MATH::Matrix4f World = MATH::Matrix4f::Identity;
	// Largest axis scale of World; object-space errors and radii are multiplied by it.
	float MaxScale = 1.0f;
	// Normal-cone culling is valid only for uniform scale without mirroring and single-sided materials.
	bool ConeCulling = false;
};

struct VirtualGeometrySelectionSettings {
	float ThresholdPixels = 1.0f;
	bool FrustumCulling = true;
	bool ConeCulling = true;
};

struct VirtualGeometrySelectionStatistics {
	uint32_t LodSelected = 0;
	uint32_t FrustumCulled = 0;
	uint32_t ConeCulled = 0;
};

VirtualGeometryInstanceTransform MakeInstanceTransform(const MATH::Matrix4f& world, bool singleSided);

// Screen-space size in pixels of an object-space error at the closest point of sphere (object space) under the
// instance transform: e * MaxScale * ProjectionScale / max(distance(camera, center) - radius, zNear) for
// perspective views and e * MaxScale * ProjectionScale for orthographic ones. VMeshRootError projects to +inf.
float ProjectedErrorPixels(float error, const VMeshSphere& sphere, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view);

// LOD cut test of one cluster with its own (Error, LodBounds) and (ParentError, ParentLodBounds):
// Proj(Error, LodBounds) <= threshold < Proj(ParentError, ParentLodBounds).
bool IsClusterInLodCut(
	const VMeshCluster& cluster, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view, float thresholdPixels);

// Appends the indices of every cluster in the LOD cut (no culling).
void SelectLodCut(const std::vector<VMeshCluster>& clusters, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view,
	float thresholdPixels, std::vector<uint32_t>& selected);

// Removes clusters outside the frustum (Bounds) and back-facing clusters (normal cone) from the list in place.
void CullClusters(const std::vector<VMeshCluster>& clusters, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view,
	const VirtualGeometrySelectionSettings& settings, std::vector<uint32_t>& selected, VirtualGeometrySelectionStatistics& statistics);

// True when the instance bounding sphere intersects the view frustum.
bool IsInstanceVisible(const VMeshSphere& bounds, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view);

// Upper bound of the clusters of each material slot that one instance can contribute to any LOD cut (sizes the
// per-batch draw segments of the GPU selection).
struct VirtualGeometrySlotCutBounds {
	// Bound per material slot: the leaf-cluster count when Proven, otherwise every cluster of the slot.
	std::vector<uint32_t> Bound = {};
	std::vector<uint32_t> Clusters = {};
	std::vector<uint32_t> Leaves = {};
	std::vector<bool> Proven = {};
	// Groups with more parents than children (or parents without children) plus clusters with non-monotone
	// error/bounds; zero for a well-formed DAG.
	uint32_t ViolatingGroups = 0;
};

// A cut is a set of clusters that covers every leaf lineage once. Starting from the all-leaves cut, every valid
// cut is reached by coarsening groups one by one (the clusters of a group are replaced by the clusters its
// simplification produced); each step changes the size of slot s by parents_s - children_s. The leaf count of a
// slot therefore bounds every cut when, for every group, parents_s <= children_s. The cut chosen by the
// per-cluster test is a valid cut when errors and bounds are monotone (Error <= ParentError and LodBounds inside
// ParentLodBounds), so those are checked too. Groups are matched by the exact bits of (ParentError,
// ParentLodBounds) of the children and (Error, LodBounds) of the parents. Any violation falls back to the
// cluster count of the affected slot, which bounds every selection because a cluster is drawn at most once.
VirtualGeometrySlotCutBounds ComputeSlotCutBounds(const std::vector<VMeshCluster>& clusters, uint32_t materialSlotCount);
}
