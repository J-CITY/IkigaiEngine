#pragma once
#include "mathModule/math.h"
#include <array>
#include <span>
#include <vector>

namespace IKIGAI::COLLISION {
// One connected, consistently oriented closed triangle manifold after position welding.
// Self-intersections are NOT tested. Mass properties require a non-self-intersecting solid.
// Render vertices/UV seams remain in the source mesh; this is a separate geometric topology.
// Unused welded vertices remain in vertices/remap, but do not contribute to mass properties.
struct ClosedSurfaceMesh {
	std::vector<MATH::Vector3f> vertices {};
	std::vector<std::array<unsigned, 3>> triangles {};
	std::vector<unsigned> sourceVertexToVertex {};
	double volume = 0;
	MATH::Vector3f center {};
	MATH::Matrix3f inertia { 0.f }; // Per unit mass about center, in source axes.
};

// Rejects open/nonmanifold/disconnected/inward surfaces, invalid indices and collapsed triangles.
// Supports concavity, but not separate/nested shells; this does not create a physics collider.
ClosedSurfaceMesh cookClosedSurfaceMesh(std::span<const MATH::Vector3f> positions, std::span<const unsigned> indices, float epsilon = 1e-5f);
}
