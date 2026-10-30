#pragma once
#include "environment.h"

namespace IKIGAI::ENVIRONMENT {
struct AtmosphereCacheUpdate {
	bool Transmittance = false;
	bool SkyView = false;
	bool AerialPerspective = false;
	bool Environment = false;
	bool ResetHistory = false;
};

class AtmosphereCache {
public:
	AtmosphereCacheUpdate update(const Settings& settings, const GpuState& state, float cameraHeight, bool forceLuts, bool forceEnvironment = false);
	float referenceHeight() const;

private:
	bool mValid = false;
	GpuState mPreviousState = {};
	GpuState mSkyState = {};
	GpuState mEnvironmentState = {};
	float mSkyTime = 0.0f;
	float mEnvironmentTime = 0.0f;
	float mReferenceHeight = 0.0f;
	float mEnvironmentHeight = 0.0f;
	float mAerialFogDensity = 0.0f;
	bool mAerialEnabled = false;
	bool mEnvironmentAerialEnabled = false;
};
}
