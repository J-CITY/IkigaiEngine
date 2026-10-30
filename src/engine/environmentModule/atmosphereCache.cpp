#include "atmosphereCache.h"
#include <algorithm>
#include <cmath>

namespace IKIGAI::ENVIRONMENT {
namespace {
	float directionDifference(const MATH::Vector4f& a, const MATH::Vector4f& b) {
		const auto delta = a - b;
		return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
	}
	bool gasChanged(const GpuState& a, const GpuState& b) {
		return a.Atmosphere.y != b.Atmosphere.y || a.Atmosphere.z != b.Atmosphere.z;
	}
	bool sunlightChanged(const GpuState& a, const GpuState& b, float threshold) {
		return directionDifference(a.SunDirection, b.SunDirection) >= threshold * threshold || std::abs(a.SunDirection.w - b.SunDirection.w) > .01f
			|| directionDifference(a.SunColor, b.SunColor) > .0001f;
	}
	bool cloudsChanged(const GpuState& a, const GpuState& b) {
		return a.CloudLayer.x != b.CloudLayer.x || a.CloudLayer.y != b.CloudLayer.y || a.CloudLayer.z != b.CloudLayer.z
			|| a.CloudLayer.w != b.CloudLayer.w || a.CloudShape.x != b.CloudShape.x || a.CloudShape.y != b.CloudShape.y
			|| std::abs(a.CloudOffset.x - b.CloudOffset.x) > 2 || std::abs(a.CloudOffset.y - b.CloudOffset.y) > 2;
	}
}
AtmosphereCacheUpdate AtmosphereCache::update(
	const Settings& settings, const GpuState& state, float cameraHeight, bool forceLuts, bool forceEnvironment) {
	const float height = std::isfinite(cameraHeight) ? std::clamp(cameraHeight, 0.01f, 399.0f) : 0.01f;
	const float time = state.Timing.x;
	const float hoursDifference = std::abs(state.Timing.z - mPreviousState.Timing.z);
	const bool discontinuity = !mValid || state.Atmosphere.x != mPreviousState.Atmosphere.x
		|| std::min(hoursDifference, std::abs(24.0f - hoursDifference)) > .5f || time < mPreviousState.Timing.x;
	const bool gas = !mValid || gasChanged(state, mSkyState);
	const bool heightChanged = std::abs(height - mReferenceHeight) >= settings.AtmosphereHeightThreshold;
	const bool skyElapsed = time - mSkyTime >= settings.AtmosphereUpdateSeconds;
	const bool environmentElapsed = time - mEnvironmentTime >= settings.IblUpdateSeconds;
	AtmosphereCacheUpdate result;
	result.Transmittance = gas || forceLuts;
	result.SkyView = result.Transmittance || discontinuity || heightChanged
		|| (skyElapsed && sunlightChanged(state, mSkyState, settings.AtmosphereSunThreshold));
	result.AerialPerspective = result.SkyView || state.Atmosphere.w != mAerialFogDensity || settings.AerialPerspective != mAerialEnabled;
	result.Environment = forceEnvironment || discontinuity
		|| (environmentElapsed
			&& (gasChanged(state, mEnvironmentState) || sunlightChanged(state, mEnvironmentState, settings.IblSunThreshold)
				|| state.SunColor.w != mEnvironmentState.SunColor.w || cloudsChanged(state, mEnvironmentState)
				|| state.Atmosphere.w != mEnvironmentState.Atmosphere.w || settings.AerialPerspective != mEnvironmentAerialEnabled
				|| std::abs(height - mEnvironmentHeight) >= settings.AtmosphereHeightThreshold));
	result.ResetHistory = discontinuity;
	if (result.SkyView) {
		mReferenceHeight = height;
		mSkyState = state;
		mSkyTime = time;
	}
	if (result.Environment) {
		mEnvironmentState = state;
		mEnvironmentTime = time;
		mEnvironmentHeight = height;
		mEnvironmentAerialEnabled = settings.AerialPerspective;
	}
	if (result.AerialPerspective) {
		mAerialFogDensity = state.Atmosphere.w;
		mAerialEnabled = settings.AerialPerspective;
	}
	mPreviousState = state;
	mValid = true;
	return result;
}
float AtmosphereCache::referenceHeight() const {
	return mReferenceHeight;
}
}
