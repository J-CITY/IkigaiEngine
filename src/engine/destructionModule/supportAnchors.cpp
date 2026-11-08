#include "supportAnchors.h"
#include "collisionModule/compoundCollider.h"
#include "destructibleDefinition.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace IKIGAI::DESTRUCTION {
namespace {
	using Vec3d = MATH::Vector3<double>;

	constexpr double MinimumOverlap = 1e-6;

	bool finite(MATH::Vector3f vector) {
		return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
	}

	Vec3d widen(MATH::Vector3f v) {
		return { v.x, v.y, v.z };
	}

	double dot(Vec3d a, Vec3d b) {
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	Vec3d unit(Vec3d axis, double lengthSquared) {
		const double inverseLength = 1.0 / std::sqrt(lengthSquared);
		return { axis.x * inverseLength, axis.y * inverseLength, axis.z * inverseLength };
	}

	bool overlapsLeaf(const COLLISION::ShapeView3D& leaf, const AnchorVolume& anchor, const std::array<MATH::Vector3f, 3>& worldBoxAxes) {
		if (leaf.shape.kind != COLLISION::Shape3D::Kind::Convex) {
			return false;
		}
		const auto& hull = *leaf.shape.polyhedron;
		// Translate before projecting so far-away world origins don't dominate the interval arithmetic.
		auto localDirection = [&](Vec3d direction) {
			return Vec3d { dot(direction, widen(leaf.axes[0])), dot(direction, widen(leaf.axes[1])), dot(direction, widen(leaf.axes[2])) };
		};
		const auto center = localDirection(widen(anchor.center) - widen(leaf.position));
		const std::array boxAxes { localDirection(widen(worldBoxAxes[0])), localDirection(widen(worldBoxAxes[1])),
			localDirection(widen(worldBoxAxes[2])) };
		auto penetratesOn = [&](Vec3d axis) {
			const double lengthSquared = dot(axis, axis);
			if (lengthSquared <= 1e-24) {
				return true; // Parallel edges contribute no separating axis.
			}
			axis = unit(axis, lengthSquared);
			double hullMin = std::numeric_limits<double>::infinity();
			double hullMax = -hullMin;
			for (const auto vertex : hull.vertices) {
				const auto projection = dot(axis, widen(vertex));
				hullMin = std::min(hullMin, projection);
				hullMax = std::max(hullMax, projection);
			}
			const double boxCenter = dot(axis, center);
			const double boxRadius = std::abs(dot(axis, boxAxes[0])) * anchor.halfExtents.x + std::abs(dot(axis, boxAxes[1])) * anchor.halfExtents.y
				+ std::abs(dot(axis, boxAxes[2])) * anchor.halfExtents.z;
			// Test the intersection width, including the case where either interval contains the other.
			return std::min(hullMax, boxCenter + boxRadius) - std::max(hullMin, boxCenter - boxRadius) > MinimumOverlap;
		};
		for (const auto axis : boxAxes) {
			if (!penetratesOn(axis)) {
				return false;
			}
		}
		for (const auto& face : hull.faces) {
			if (!penetratesOn(widen(face.normal))) {
				return false;
			}
		}
		for (const auto& edge : hull.edges) {
			auto direction = widen(hull.vertices[edge[1]]) - widen(hull.vertices[edge[0]]);
			direction = unit(direction, dot(direction, direction));
			for (const auto axis : boxAxes) {
				if (!penetratesOn(direction.cross(axis))) {
					return false;
				}
			}
		}
		return true;
	}
}

AnchorVolume makeWorldSupportAnchor(const SupportAnchorDesc& anchor, MATH::Vector3f rootPosition, MATH::QuaternionF rootOrientation) {
	const auto rootLength = MATH::QuaternionF::Length(rootOrientation);
	if (!finite(rootPosition) || !std::isfinite(rootLength) || std::abs(rootLength - 1.f) > 1e-4f) {
		throw std::invalid_argument("Support anchor requires a finite, unit root pose");
	}
	const auto worldCenter = rootPosition + rootOrientation * anchor.center;
	const auto worldOrientation = rootOrientation * anchor.orientation;
	const auto worldLength = MATH::QuaternionF::Length(worldOrientation);
	if (!finite(worldCenter) || !std::isfinite(worldLength) || std::abs(worldLength - 1.f) > 2e-4f) {
		throw std::invalid_argument("Support anchor world pose is not finite and unit");
	}
	return { anchor.id, worldCenter, anchor.halfExtents, MATH::QuaternionF::Normalize(worldOrientation), anchor.enabled };
}

bool overlapsSupportAnchor(const COLLISION::ShapeView3D& material, const AnchorVolume& anchor) {
	if (!anchor.enabled) {
		return false;
	}
	const std::array<MATH::Vector3f, 3> boxAxes { anchor.orientation * MATH::Vector3f { 1, 0, 0 }, anchor.orientation * MATH::Vector3f { 0, 1, 0 },
		anchor.orientation * MATH::Vector3f { 0, 0, 1 } };
	for (std::size_t child = 0; child < COLLISION::compoundLeafCount(material); ++child) {
		if (overlapsLeaf(COLLISION::compoundLeafView(material, child), anchor, boxAxes)) {
			return true;
		}
	}
	return false;
}
}
