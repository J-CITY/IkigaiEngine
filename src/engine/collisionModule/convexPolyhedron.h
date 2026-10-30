#pragma once
#include "mathModule/math.h"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace IKIGAI::COLLISION {
struct ConvexFace {
	MATH::Vector3f normal {};
	float offset = 0;
	std::vector<unsigned> vertices {};
};
// Immutable, closed, outward-wound hull. Inertia is per unit mass about COM.
struct ConvexPolyhedron {
	std::vector<MATH::Vector3f> vertices {};
	std::vector<std::array<unsigned, 3>> triangles {};
	std::vector<ConvexFace> faces {};
	std::vector<std::array<unsigned, 2>> edges {};
	double volume = 0;
	MATH::Vector3f center {};
	MATH::Matrix3f inertia { 0.f };
};
std::shared_ptr<const ConvexPolyhedron> buildConvexPolyhedron(
	std::span<const MATH::Vector3f> vertices, std::span<const unsigned> indices, float epsilon = 1e-5f);
MATH::Vector3f closestTrianglePoint(MATH::Vector3f point, MATH::Vector3f a, MATH::Vector3f b, MATH::Vector3f c);
}
