#pragma once

#include "mathModule/math.h"

#include <array>

namespace IKIGAI::ENVIRONMENT {
struct VegetationInteractionSettings {
	float Radius = 0.5f;
	float Height = 1.8f;
	float Strength = 1.0f;
	float RecoverySeconds = 0.5f;
	MATH::Vector3f Offset {};
};

struct VegetationInteractionSample {
	MATH::Vector4f StartRadius {};
	MATH::Vector4f EndStrength {};
};

struct VegetationInteractionTrail {
	std::array<VegetationInteractionSample, 4> current {};
	std::array<VegetationInteractionSample, 4> previous {};
	float time = 0.0f;
};

void advanceVegetationInteractionTrail(
	VegetationInteractionTrail& trail, const VegetationInteractionSettings& settings, const MATH::Matrix4f& worldMatrix, float elapsedSeconds);
}
