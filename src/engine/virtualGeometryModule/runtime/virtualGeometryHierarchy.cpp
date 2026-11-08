#include "virtualGeometryHierarchy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace IKIGAI::VG {
namespace {
	constexpr uint32_t LeafCapacity = 8;
	using Point = std::array<double, 3>;

	struct CenterExtents {
		Point Min = {};
		Point Max = {};

		explicit CenterExtents(const VMeshSphere& sphere)
			: Min { sphere.Center[0], sphere.Center[1], sphere.Center[2] }
			, Max(Min) { }

		void include(const VMeshSphere& sphere) {
			for (size_t axis = 0; axis < Min.size(); ++axis) {
				Min[axis] = std::min(Min[axis], double(sphere.Center[axis]));
				Max[axis] = std::max(Max[axis], double(sphere.Center[axis]));
			}
		}

		VMeshSphere midpoint() const {
			VMeshSphere sphere;
			for (size_t axis = 0; axis < Min.size(); ++axis) {
				sphere.Center[axis] = static_cast<float>((Min[axis] + Max[axis]) * 0.5);
			}
			return sphere;
		}

		size_t longestAxis() const {
			size_t longest = 0;
			for (size_t axis = 1; axis < Min.size(); ++axis) {
				if (Max[axis] - Min[axis] > Max[longest] - Min[longest]) {
					longest = axis;
				}
			}
			return longest;
		}
	};

	double CenterDistance(const VMeshSphere& a, const VMeshSphere& b) {
		const double x = double(a.Center[0]) - b.Center[0];
		const double y = double(a.Center[1]) - b.Center[1];
		const double z = double(a.Center[2]) - b.Center[2];
		return std::sqrt(x * x + y * y + z * z);
	}

	float RoundRadiusUp(double radius) {
		if (radius >= std::numeric_limits<float>::max()) {
			return std::numeric_limits<float>::infinity();
		}
		return std::nextafter(static_cast<float>(radius), std::numeric_limits<float>::infinity());
	}

	void BuildNode(VirtualGeometryHierarchy& hierarchy, const std::vector<VMeshCluster>& clusters, uint32_t first, uint32_t count) {
		const uint32_t nodeIndex = static_cast<uint32_t>(hierarchy.Nodes.size());
		hierarchy.Nodes.emplace_back();

		VirtualGeometryHierarchyNode node;
		node.FirstCluster = first;
		node.ClusterCount = count;
		const auto begin = hierarchy.ClusterIds.begin() + first;
		const auto end = begin + count;
		const auto& firstCluster = clusters[*begin];
		CenterExtents geometry(firstCluster.Bounds);
		CenterExtents lod(firstCluster.LodBounds);
		node.MinError = firstCluster.Error;
		node.MaxParentError = firstCluster.ParentError;
		for (auto it = begin + 1; it != end; ++it) {
			const auto& cluster = clusters[*it];
			geometry.include(cluster.Bounds);
			lod.include(cluster.LodBounds);
			node.MinError = std::min(node.MinError, cluster.Error);
			node.MaxParentError = std::max(node.MaxParentError, cluster.ParentError);
		}

		// Recompute reach about the stored float centers, then round outwards. Geometry contains
		// whole spheres. The LOD lower bound needs only the centers of the own-error spheres.
		node.Bounds = geometry.midpoint();
		node.LodCenters = lod.midpoint();
		double geometryRadius = 0.0;
		double lodRadius = 0.0;
		for (auto it = begin; it != end; ++it) {
			const auto& cluster = clusters[*it];
			geometryRadius = std::max(geometryRadius, CenterDistance(node.Bounds, cluster.Bounds) + cluster.Bounds.Radius);
			lodRadius = std::max(lodRadius, CenterDistance(node.LodCenters, cluster.LodBounds));
		}
		node.Bounds.Radius = RoundRadiusUp(geometryRadius);
		node.LodCenters.Radius = RoundRadiusUp(lodRadius);

		if (count > LeafCapacity) {
			const uint32_t leftCount = count / 2;
			const size_t axis = geometry.longestAxis();
			std::nth_element(begin, begin + leftCount, end, [&](uint32_t lhsId, uint32_t rhsId) {
				const auto& lhs = clusters[lhsId];
				const auto& rhs = clusters[rhsId];
				// Keep similar levels together so fine clusters with zero error and roots with
				// unbounded parent error do not prevent pruning across unrelated spatial branches.
				if (lhs.LodLevel != rhs.LodLevel) {
					return lhs.LodLevel < rhs.LodLevel;
				}
				if (lhs.Bounds.Center[axis] != rhs.Bounds.Center[axis]) {
					return lhs.Bounds.Center[axis] < rhs.Bounds.Center[axis];
				}
				return lhsId < rhsId;
			});
			BuildNode(hierarchy, clusters, first, leftCount);
			BuildNode(hierarchy, clusters, first + leftCount, count - leftCount);
		}
		else {
			// nth_element determines membership with a total order, while leaving bucket order
			// unspecified. Canonicalize each leaf's order for deterministic serialized GPU data.
			std::sort(begin, end);
		}
		node.Escape = static_cast<uint32_t>(hierarchy.Nodes.size());
		hierarchy.Nodes[nodeIndex] = node;
	}

	// Upper bound on the distance computed by the leaf projection. The linear transform expands
	// the LOD center sphere by at most the Frobenius norm, including for sheared transforms.
	float FarthestLodDistance(const VMeshSphere& centers, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view) {
		const auto matrix = instance.World.getData();
		const Point camera { view.CameraPosition.x, view.CameraPosition.y, view.CameraPosition.z };
		double distanceSquared = 0.0;
		double normSquared = 0.0;
		double arithmeticMagnitude = 0.0;
		for (size_t row = 0; row < camera.size(); ++row) {
			double transformed = double(matrix[row * 4 + 3]) - camera[row];
			arithmeticMagnitude += std::abs(matrix[row * 4 + 3]) + std::abs(camera[row]);
			for (size_t col = 0; col < camera.size(); ++col) {
				const double coefficient = matrix[row * 4 + col];
				transformed += coefficient * centers.Center[col];
				normSquared += coefficient * coefficient;
				arithmeticMagnitude += std::abs(coefficient) * (std::abs(double(centers.Center[col])) + centers.Radius);
			}
			distanceSquared += transformed * transformed;
		}
		const double reach = std::sqrt(distanceSquared) + std::sqrt(normSquared) * centers.Radius;
		// The relative margin covers rounding in the float transform, subtract, dot product and sqrt.
		// The absolute margin covers the sqrt of the rounding error of a subnormal squared length.
		// Reject the estimate if intermediate values might overflow the leaf's float squared length.
		const double upper = reach + (reach + arithmeticMagnitude) * 1e-5 + 1e-18;
		if (!(upper < 1e18)) {
			return std::numeric_limits<float>::infinity();
		}
		return RoundRadiusUp(upper);
	}

	bool RejectLod(const VirtualGeometryHierarchyNode& node, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view,
		float thresholdPixels) {
		if (!(instance.MaxScale >= 0.0f && view.ProjectionScale >= 0.0f && std::isfinite(thresholdPixels))) {
			return false;
		}
		if (!view.Orthographic && !(view.ZNear > 0.0f)) {
			return false;
		}
		if (node.MaxParentError < VMeshRootError) {
			// The leaf clamps its perspective denominator to ZNear. Increasing its error to
			// MaxParentError and replacing the denominator with that floor can only raise projection.
			const float parentPixels = (node.MaxParentError * instance.MaxScale) * view.ProjectionScale;
			if ((view.Orthographic ? parentPixels : parentPixels / view.ZNear) <= thresholdPixels) {
				return true;
			}
		}
		if (node.MinError > 0.0f) {
			const float ownPixels = (node.MinError * instance.MaxScale) * view.ProjectionScale;
			if (view.Orthographic) {
				return ownPixels > thresholdPixels;
			}
			// A nonnegative leaf radius is subtracted from its distance. Using the farthest
			// distance to any leaf center therefore overestimates every own-error denominator.
			const float denominator = std::max(FarthestLodDistance(node.LodCenters, instance, view), view.ZNear);
			return ownPixels / denominator > thresholdPixels;
		}
		return false;
	}
}

size_t VirtualGeometryHierarchy::bytes() const {
	return Nodes.size() * sizeof(VirtualGeometryHierarchyNode) + ClusterIds.size() * sizeof(uint32_t);
}

VirtualGeometryHierarchy BuildVirtualGeometryHierarchy(const std::vector<VMeshCluster>& clusters) {
	VirtualGeometryHierarchy hierarchy;
	// Every nontrivial leaf contains at least four IDs, so the number of nodes fits whenever the
	// table IDs fit.
	if (clusters.size() > std::numeric_limits<uint32_t>::max()) {
		throw std::length_error("Virtual geometry hierarchy exceeds the cluster ID range");
	}
	if (!clusters.empty()) {
		hierarchy.ClusterIds.resize(clusters.size());
		std::iota(hierarchy.ClusterIds.begin(), hierarchy.ClusterIds.end(), 0u);
		BuildNode(hierarchy, clusters, 0, static_cast<uint32_t>(clusters.size()));
	}
	return hierarchy;
}

VirtualGeometryHierarchyStatistics SelectHierarchyLodCut(const VirtualGeometryHierarchy& hierarchy, const std::vector<VMeshCluster>& clusters,
	const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view, float thresholdPixels, std::vector<uint32_t>& selected,
	uint32_t visitBudget) {
	VirtualGeometryHierarchyStatistics statistics;
	const size_t originalSize = selected.size();
	uint32_t index = 0;
	while (index < hierarchy.Nodes.size() && statistics.NodesVisited < visitBudget) {
		const auto& node = hierarchy.Nodes[index];
		++statistics.NodesVisited;
		if (RejectLod(node, instance, view, thresholdPixels)) {
			++statistics.NodesLodRejected;
			index = node.Escape;
			continue;
		}
		if (node.Escape == index + 1) {
			++statistics.LeavesVisited;
			statistics.ClustersTested += node.ClusterCount;
			for (uint32_t offset = 0; offset < node.ClusterCount; ++offset) {
				const uint32_t clusterId = hierarchy.ClusterIds[node.FirstCluster + offset];
				if (IsClusterInLodCut(clusters[clusterId], instance, view, thresholdPixels)) {
					selected.push_back(clusterId);
				}
			}
		}
		++index;
	}
	if (index < hierarchy.Nodes.size() || (hierarchy.Nodes.empty() && !clusters.empty())) {
		selected.resize(originalSize);
		SelectLodCut(clusters, instance, view, thresholdPixels, selected);
		statistics.ClustersTested += clusters.size();
		statistics.UsedFlatFallback = true;
	}
	else {
		std::sort(selected.begin() + originalSize, selected.end());
	}
	return statistics;
}
}
