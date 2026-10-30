#pragma once
#include "surfaceSettings.h"
#include <array>
#include <atomic>

namespace IKIGAI::SURFACE {
struct Coverage {
	std::array<float, 4> Weights = { 1, 0, 0, 0 };
	float FoliageDensity = 1.0f;
};
// RGBA stores three ordered compositing alphas and the foliage density mask.
std::vector<uint8_t> buildCoverageMap(const Settings& settings, const std::atomic_bool* cancellation = nullptr);
Coverage coverage(const Settings& settings, float x, float z);
MATH::Vector4f coverageUv(const Settings& settings);
}
