#include "environmentPresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace IKIGAI::ENVIRONMENT {
namespace {
	constexpr std::array<std::string_view, 7> Names = { "Custom", "Clear", "WhiteClouds", "Overcast", "Storm", "Rain", "Snow" };
	float smooth(float value) {
		value = std::clamp(value, 0.0f, 1.0f);
		return value * value * (3.0f - 2.0f * value);
	}
}
void from_json(const nlohmann::json& json, Preset& value) {
	const auto name = json.get<std::string>();
	const auto found = std::find(Names.begin(), Names.end(), name);
	if (found == Names.end()) {
		throw std::invalid_argument("Unknown Environment preset: " + name);
	}
	value = Preset(found - Names.begin());
}
void to_json(nlohmann::json& json, const Preset& value) {
	const auto index = size_t(value);
	if (index >= Names.size()) {
		throw std::invalid_argument("Invalid Environment preset");
	}
	json = Names[index];
}
Settings presetSettings(Preset preset, const Settings& base) {
	base.validate();
	if (size_t(preset) >= Names.size()) {
		throw std::invalid_argument("Invalid Environment preset");
	}
	if (preset == Preset::Custom) {
		return base;
	}
	auto result = base;
	result.AtmosphereDensity = 1.0f;
	result.Turbidity = 2.0f;
	result.FogDensity = .0003f;
	result.Clouds = CloudType::Cumulus;
	result.CloudBottom = 80.0f;
	result.CloudTop = 160.0f;
	result.CloudCoverage = .55f;
	result.CloudDensity = .025f;
	result.CloudDarkness = 0.0f;
	result.CloudShadowStrength = .75f;
	result.WindSpeed = 1.5f;
	result.CloudWindMultiplier = 1.0f;
	result.GustStrength = .3f;
	result.Precipitation = Weather::Clear;
	result.PrecipitationIntensity = 0.0f;
	switch (preset) {
	case Preset::Clear:
		result.CloudCoverage = 0.0f;
		result.FogDensity = .00015f;
		break;
	case Preset::WhiteClouds:
		break;
	case Preset::Overcast:
	case Preset::Snow:
		result.Clouds = CloudType::Stratus;
		result.CloudCoverage = .9f;
		result.CloudDensity = .04f;
		result.CloudBottom = 60.0f;
		result.CloudTop = 130.0f;
		result.CloudDarkness = .35f;
		result.Turbidity = 2.5f;
		result.FogDensity = .0002f;
		result.WindSpeed = 2.5f;
		if (preset == Preset::Snow) {
			result.Precipitation = Weather::Snow;
			result.PrecipitationIntensity = .7f;
		}
		break;
	case Preset::Storm:
	case Preset::Rain:
		result.Clouds = CloudType::Storm;
		result.CloudCoverage = .95f;
		result.CloudDensity = .05f;
		result.CloudBottom = 50.0f;
		result.CloudTop = 180.0f;
		result.CloudDarkness = .85f;
		result.CloudShadowStrength = .9f;
		result.Turbidity = 3.0f;
		result.FogDensity = .0001f;
		result.WindSpeed = 8.0f;
		result.GustStrength = 1.0f;
		if (preset == Preset::Rain) {
			result.Precipitation = Weather::Rain;
			result.PrecipitationIntensity = .8f;
		}
		break;
	case Preset::Custom:
		break;
	}
	result.validate();
	return result;
}
void PresetTransition::reset(const Settings& settings) {
	settings.validate();
	mCurrent = settings;
	mStart = settings;
	mTarget = settings;
	mElapsed = 0.0;
	mDuration = 0.0f;
}
void PresetTransition::start(const Settings& target, float seconds) {
	target.validate();
	if (!std::isfinite(seconds) || seconds < 0 || seconds > 3600) {
		throw std::invalid_argument("Environment preset duration must be in [0, 3600] seconds");
	}
	mStart = mCurrent;
	mTarget = target;
	mDuration = seconds;
	mElapsed = 0.0;
	update(0.0);
}
void PresetTransition::update(double seconds) {
	mElapsed = std::min(double(mDuration), mElapsed + (std::isfinite(seconds) ? std::clamp(seconds, 0.0, .25) : 0.0));
	const float linear = progress();
	const float weight = smooth(linear);
	mCurrent = mTarget;
	auto blend = [weight](float start, float target) { return start + (target - start) * weight; };
	// Presets own these continuous controls; time, sun, seed and budgets retain
	// their authored values. Precipitation uses Runtime's existing independent ramp.
	mCurrent.Turbidity = blend(mStart.Turbidity, mTarget.Turbidity);
	mCurrent.AtmosphereDensity = blend(mStart.AtmosphereDensity, mTarget.AtmosphereDensity);
	mCurrent.FogDensity = blend(mStart.FogDensity, mTarget.FogDensity);
	mCurrent.CloudBottom = blend(mStart.CloudBottom, mTarget.CloudBottom);
	mCurrent.CloudTop = blend(mStart.CloudTop, mTarget.CloudTop);
	mCurrent.CloudDensity = blend(mStart.CloudDensity, mTarget.CloudDensity);
	mCurrent.CloudDarkness = blend(mStart.CloudDarkness, mTarget.CloudDarkness);
	mCurrent.CloudShadowStrength = blend(mStart.CloudShadowStrength, mTarget.CloudShadowStrength);
	mCurrent.WindSpeed = blend(mStart.WindSpeed, mTarget.WindSpeed);
	mCurrent.CloudWindMultiplier = blend(mStart.CloudWindMultiplier, mTarget.CloudWindMultiplier);
	mCurrent.GustStrength = blend(mStart.GustStrength, mTarget.GustStrength);
	if (mStart.Clouds == mTarget.Clouds) {
		mCurrent.CloudCoverage = blend(mStart.CloudCoverage, mTarget.CloudCoverage);
	}
	else {
		// The cloud model is discrete. Exchange it only while coverage is zero,
		// avoiding a density/profile pop in clouds and their shadows.
		mCurrent.Clouds = linear < .5f ? mStart.Clouds : mTarget.Clouds;
		mCurrent.CloudCoverage = linear < .5f ? mStart.CloudCoverage * (1 - smooth(linear * 2)) : mTarget.CloudCoverage * smooth(linear * 2 - 1);
	}
	if (!active()) {
		mCurrent = mTarget;
	}
}
const Settings& PresetTransition::current() const {
	return mCurrent;
}
float PresetTransition::progress() const {
	return mDuration > 0 ? float(mElapsed / mDuration) : 1.0f;
}
bool PresetTransition::active() const {
	return mElapsed < mDuration;
}
}
