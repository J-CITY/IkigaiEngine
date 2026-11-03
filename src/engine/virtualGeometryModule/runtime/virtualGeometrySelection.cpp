#include "virtualGeometrySelection.h"

#include "renderModule/frustum.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace IKIGAI::VG {
namespace {
	// Row-major element access on const matrices (Matrix4f::operator() is non-const).
	float At(const MATH::Matrix4f& matrix, int row, int column) {
		return matrix.getData()[row * 4 + column];
	}

	MATH::Vector3f TransformPoint(const MATH::Matrix4f& matrix, const float point[3]) {
		return matrix * MATH::Vector3f { point[0], point[1], point[2] };
	}

	MATH::Vector3f TransformDirection(const MATH::Matrix4f& matrix, const float direction[3]) {
		return { At(matrix, 0, 0) * direction[0] + At(matrix, 0, 1) * direction[1] + At(matrix, 0, 2) * direction[2],
			At(matrix, 1, 0) * direction[0] + At(matrix, 1, 1) * direction[1] + At(matrix, 1, 2) * direction[2],
			At(matrix, 2, 0) * direction[0] + At(matrix, 2, 1) * direction[1] + At(matrix, 2, 2) * direction[2] };
	}

	bool SphereVisible(const MATH::Vector3f& center, float radius, const VirtualGeometryView& view) {
		return !view.Frustum || view.Frustum->sphereInFrustum(center.x, center.y, center.z, radius);
	}
}

VirtualGeometryInstanceTransform MakeInstanceTransform(const MATH::Matrix4f& world, bool singleSided) {
	VirtualGeometryInstanceTransform result;
	result.World = world;
	const MATH::Vector3f axisX { At(world, 0, 0), At(world, 1, 0), At(world, 2, 0) };
	const MATH::Vector3f axisY { At(world, 0, 1), At(world, 1, 1), At(world, 2, 1) };
	const MATH::Vector3f axisZ { At(world, 0, 2), At(world, 1, 2), At(world, 2, 2) };
	const float lengthX = MATH::Vector3f::Length(axisX);
	const float lengthY = MATH::Vector3f::Length(axisY);
	const float lengthZ = MATH::Vector3f::Length(axisZ);
	result.MaxScale = std::max({ lengthX, lengthY, lengthZ });
	const float minScale = std::min({ lengthX, lengthY, lengthZ });
	const float determinant = MATH::Vector3f::Dot(MATH::Vector3f::Cross(axisX, axisY), axisZ);
	result.ConeCulling = singleSided && minScale > 0.0f && result.MaxScale <= minScale * 1.001f && determinant > 0.0f;
	if (!std::isfinite(result.MaxScale)) {
		result.MaxScale = 0.0f;
		result.ConeCulling = false;
	}
	return result;
}

float ProjectedErrorPixels(
	float error, const VMeshSphere& sphere, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view) {
	if (error >= VMeshRootError) {
		return std::numeric_limits<float>::infinity();
	}
	const float worldError = error * instance.MaxScale;
	if (view.Orthographic) {
		return worldError * view.ProjectionScale;
	}
	const auto center = TransformPoint(instance.World, sphere.Center);
	const float distance = MATH::Vector3f::Length(center - view.CameraPosition) - sphere.Radius * instance.MaxScale;
	return worldError * view.ProjectionScale / std::max(distance, view.ZNear);
}

bool IsClusterInLodCut(
	const VMeshCluster& cluster, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view, float thresholdPixels) {
	// Parent first: most clusters of a deep DAG are rejected because a coarser level is already accurate enough.
	if (!(ProjectedErrorPixels(cluster.ParentError, cluster.ParentLodBounds, instance, view) > thresholdPixels)) {
		return false;
	}
	return ProjectedErrorPixels(cluster.Error, cluster.LodBounds, instance, view) <= thresholdPixels;
}

void SelectLodCut(const std::vector<VMeshCluster>& clusters, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view,
	float thresholdPixels, std::vector<uint32_t>& selected) {
	for (uint32_t index = 0; index < clusters.size(); ++index) {
		if (IsClusterInLodCut(clusters[index], instance, view, thresholdPixels)) {
			selected.push_back(index);
		}
	}
}

void CullClusters(const std::vector<VMeshCluster>& clusters, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view,
	const VirtualGeometrySelectionSettings& settings, std::vector<uint32_t>& selected, VirtualGeometrySelectionStatistics& statistics) {
	const bool coneCulling = settings.ConeCulling && instance.ConeCulling && !view.Orthographic;
	size_t kept = 0;
	for (const uint32_t index : selected) {
		const auto& cluster = clusters[index];
		const auto center = TransformPoint(instance.World, cluster.Bounds.Center);
		const float radius = cluster.Bounds.Radius * instance.MaxScale;
		if (settings.FrustumCulling && !SphereVisible(center, radius, view)) {
			++statistics.FrustumCulled;
			continue;
		}
		if (coneCulling && cluster.ConeCutoff < 1.0f) {
			// meshoptimizer convention: the cluster is back-facing for every viewer inside the cone
			// dot(center - camera, axis) >= cutoff * |center - camera| + radius.
			auto axis = TransformDirection(instance.World, cluster.ConeAxis);
			const float axisLength = MATH::Vector3f::Length(axis);
			if (axisLength > 0.0f) {
				axis = axis * (1.0f / axisLength);
				const auto toCenter = center - view.CameraPosition;
				if (MATH::Vector3f::Dot(toCenter, axis) >= cluster.ConeCutoff * MATH::Vector3f::Length(toCenter) + radius) {
					++statistics.ConeCulled;
					continue;
				}
			}
		}
		selected[kept++] = index;
	}
	selected.resize(kept);
}

bool IsInstanceVisible(const VMeshSphere& bounds, const VirtualGeometryInstanceTransform& instance, const VirtualGeometryView& view) {
	if (bounds.Radius <= 0.0f) {
		return true;
	}
	const auto center = TransformPoint(instance.World, bounds.Center);
	return SphereVisible(center, bounds.Radius * instance.MaxScale, view);
}
}
