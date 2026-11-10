#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "renderModule/objects/boundingSphere.h"

namespace IKIGAI::TASK {
class TaskSystem;
}

namespace IKIGAI::RENDER {
class Frustum;

struct FrustumModelQuery {
	BoundingSphere bounds {};
	MATH::Vector3f position {};
	MATH::QuaternionF rotation = MATH::QuaternionF::Identity;
	MATH::Vector3f scale { 1.0f, 1.0f, 1.0f };
	std::size_t firstMesh = 0;
	std::size_t meshCount = 0;
};

struct FrustumVisibility {
	std::vector<std::uint8_t> models {};
	std::vector<std::uint8_t> meshes {};
};

// Mesh intervals must be in bounds, disjoint and ordered by model for both overloads.
// An empty interval means model-only culling.
// A null task system evaluates the same records inline.
[[nodiscard]] FrustumVisibility evaluateFrustumVisibility(
	const Frustum& frustum, std::span<const FrustumModelQuery> models, std::span<const BoundingSphere> meshes, TASK::TaskSystem* tasks);

// Caller owns output storage through completion. Workers never allocate from its arena.
// Output spans must match the input counts; all bytes are initialized by this call.
// Outputs must not overlap each other or the input records.
void evaluateFrustumVisibility(const Frustum& frustum, std::span<const FrustumModelQuery> models, std::span<const BoundingSphere> meshes,
	std::span<std::uint8_t> visibleModels, std::span<std::uint8_t> visibleMeshes, TASK::TaskSystem* tasks);
}
