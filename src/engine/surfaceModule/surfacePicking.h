#pragma once
#include "surfaceGenerator.h"
#include <optional>

namespace IKIGAI::SURFACE {
struct SurfaceHit {
	MATH::Vector3f Position = {};
	MATH::Vector3f Normal = { 0, 1, 0 };
	float Parameter = 0;
};
// Ray is in local surface space. An unnormalised direction preserves the world-ray
// distance parameter after inverse transforms, including nonuniform scale.
std::optional<SurfaceHit> raycastSurface(
	const Settings& settings, const MATH::Vector3f& origin, const MATH::Vector3f& direction, float maximumParameter = 10000.0f);
}
