#pragma once
#include "collisionModule/collision3d.h"
#include <cstdint>

namespace IKIGAI::DESTRUCTION {
struct SupportAnchorDesc;

// A fixed world-space box resolved from the authored root pose at initialization.
struct AnchorVolume {
	std::uint64_t id = 0;
	MATH::Vector3f center {};
	MATH::Vector3f halfExtents {};
	MATH::QuaternionF orientation { 0, 0, 0, 1 };
	bool enabled = true;
};

AnchorVolume makeWorldSupportAnchor(const SupportAnchorDesc& anchor, MATH::Vector3f rootPosition, MATH::QuaternionF rootOrientation);

// Destructible material is a convex hull or a compound of convex hulls. An enabled
// anchor supports it when any one leaf overlaps by more than 1e-6 world units
// on every separating axis.
bool overlapsSupportAnchor(const COLLISION::ShapeView3D& material, const AnchorVolume& anchor);
}
