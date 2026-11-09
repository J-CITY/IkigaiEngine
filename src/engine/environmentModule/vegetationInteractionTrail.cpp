#include "vegetationInteractionTrail.h"

#include <algorithm>
#include <cmath>

namespace IKIGAI::ENVIRONMENT {
void advanceVegetationInteractionTrail(
	VegetationInteractionTrail& trail, const VegetationInteractionSettings& settings, const MATH::Matrix4f& worldMatrix, float elapsedSeconds) {
	const float dt = std::clamp(elapsedSeconds, 0.0f, 0.25f);
	trail.previous = trail.current;
	trail.time += dt;
	const float interval = std::max(settings.RecoverySeconds / 3, 0.016f);
	if (trail.time >= interval) {
		for (std::size_t index = trail.current.size() - 1; index > 0; --index) {
			trail.current[index] = trail.current[index - 1];
		}
		trail.time = std::fmod(trail.time, interval);
	}
	const float decay = dt * settings.Strength / std::max(settings.RecoverySeconds, 0.001f);
	for (std::size_t index = 1; index < trail.current.size(); ++index) {
		auto& strength = trail.current[index].EndStrength.w;
		strength = std::max(0.0f, strength - decay);
	}

	const auto start = worldMatrix * settings.Offset;
	const auto end = worldMatrix * (settings.Offset + MATH::Vector3f { 0, settings.Height, 0 });
	trail.current[0] = { { start.x, start.y, start.z, settings.Radius }, { end.x, end.y, end.z, settings.Strength } };
	if (trail.previous[0].StartRadius.w == 0) {
		trail.previous = trail.current;
	}
}
}
