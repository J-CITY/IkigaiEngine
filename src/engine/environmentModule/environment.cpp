#include "environment.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::ENVIRONMENT {
void from_json(const nlohmann::json& json, Weather& value) {
	const auto name = json.get<std::string>();
	if (name == "Clear") {
		value = Weather::Clear;
	}
	else if (name == "Rain") {
		value = Weather::Rain;
	}
	else if (name == "Snow") {
		value = Weather::Snow;
	}
	else {
		throw std::invalid_argument("Unknown Precipitation: " + name);
	}
}
void to_json(nlohmann::json& json, const Weather& value) {
	json = value == Weather::Clear ? "Clear" : value == Weather::Rain ? "Rain" : "Snow";
}
void from_json(const nlohmann::json& json, CloudType& value) {
	const auto name = json.get<std::string>();
	if (name == "Cumulus") {
		value = CloudType::Cumulus;
	}
	else if (name == "Stratus") {
		value = CloudType::Stratus;
	}
	else if (name == "Storm") {
		value = CloudType::Storm;
	}
	else {
		throw std::invalid_argument("Unknown Clouds: " + name);
	}
}
void to_json(nlohmann::json& json, const CloudType& value) {
	json = value == CloudType::Cumulus ? "Cumulus" : value == CloudType::Stratus ? "Stratus" : "Storm";
}
void Settings::validate() const {
	auto range = [](float value, float low, float high, const char* name) {
		if (!std::isfinite(value) || value < low || value > high) {
			throw std::invalid_argument(std::string("Environment.") + name + " outside range");
		}
	};
	range(TimeOfDay, 0, 24, "TimeOfDay");
	range(DayDuration, 1, 1e8f, "DayDuration");
	range(SunAzimuth, -360, 360, "SunAzimuth");
	range(SunIntensity, 0, 100000, "SunIntensity");
	range(SunAngularRadius, 0.0001f, 0.1f, "SunAngularRadius");
	range(Turbidity, 1, 20, "Turbidity");
	range(AtmosphereDensity, 0.01f, 10, "AtmosphereDensity");
	range(FogDensity, 0, 1, "FogDensity");
	range(CloudCoverage, 0, 1, "CloudCoverage");
	range(CloudDensity, 0, 10, "CloudDensity");
	range(CloudBottom, 1, 100000, "CloudBottom");
	range(CloudTop, CloudBottom + 0.01f, 200000, "CloudTop");
	range(CloudDarkness, 0, 1, "CloudDarkness");
	range(CloudShadowStrength, 0, 1, "CloudShadowStrength");
	range(WindDirection.x, -1e4f, 1e4f, "WindDirection.X");
	range(WindDirection.y, -1e4f, 1e4f, "WindDirection.Y");
	range(WindSpeed, 0, 1000, "WindSpeed");
	range(CloudWindMultiplier, 0, 100, "CloudWindMultiplier");
	range(GustStrength, 0, 10, "GustStrength");
	range(PrecipitationIntensity, 0, 1, "PrecipitationIntensity");
	range(TransitionSeconds, 0, 3600, "TransitionSeconds");
	range(PrecipitationRadius, 1, 1000, "PrecipitationRadius");
	range(PrecipitationHeight, 1, 1000, "PrecipitationHeight");
	range(RainSpeed, 0.1f, 100, "RainSpeed");
	range(SnowSpeed, 0.1f, 30, "SnowSpeed");
	range(SurfaceWeatherRadius, 1, 1000, "SurfaceWeatherRadius");
	range(WetnessRate, 0, 10, "WetnessRate");
	range(DryingRate, 0, 10, "DryingRate");
	range(SnowAccumulationRate, 0, 10, "SnowAccumulationRate");
	range(SnowMeltRate, 0, 10, "SnowMeltRate");
	range(IblUpdateSeconds, 0.1f, 3600, "IblUpdateSeconds");
	range(IblSunThreshold, 0.001f, 1, "IblSunThreshold");
	range(AtmosphereUpdateSeconds, 0, 10, "AtmosphereUpdateSeconds");
	range(AtmosphereSunThreshold, 0.0001f, 1, "AtmosphereSunThreshold");
	range(AtmosphereHeightThreshold, 0.1f, 100, "AtmosphereHeightThreshold");
	if (MaxParticles > 32768 || CollisionBudget > 4096 || SurfaceWeatherQueryBudget > 4096) {
		throw std::invalid_argument("Environment particle/query budget exceeded");
	}
}
void Runtime::update(const Settings& settings, double seconds) {
	const float dt = std::isfinite(seconds) ? static_cast<float>(std::clamp(seconds, 0.0, 0.25)) : 0.0f;
	if (mAuthoredHours != settings.TimeOfDay) {
		setTimeOfDay(settings.TimeOfDay);
		mAuthoredHours = settings.TimeOfDay;
	}
	if (settings.AnimateTime) {
		mHours = std::fmod(mHours + dt * 24 / settings.DayDuration, 24.0f);
	}
	mTime += dt;
	const float angle = (mHours - 6) * 6.28318530718f / 24;
	const float azimuth = settings.SunAzimuth * 0.01745329252f;
	const float elevation = std::sin(angle);
	const float daylight = std::clamp((elevation + 0.06f) / 0.18f, 0.0f, 1.0f);
	const float warm = std::clamp(1 - elevation * 3, 0.0f, 1.0f);
	mGpu.SunDirection = { std::cos(angle) * std::cos(azimuth), elevation, std::cos(angle) * std::sin(azimuth), settings.SunIntensity * daylight };
	mGpu.SunColor = { 1, 1 - warm * 0.42f, 1 - warm * 0.78f, settings.SunAngularRadius };
	mGpu.Atmosphere = { 1, settings.Turbidity, settings.AtmosphereDensity, settings.FogDensity };
	mGpu.CloudLayer = { settings.CloudBottom, settings.CloudTop, settings.CloudCoverage, settings.CloudDensity };
	mGpu.CloudShape = { float(settings.Clouds), settings.CloudDarkness, settings.CloudShadowStrength, daylight };
	const float windLength = std::hypot(settings.WindDirection.x, settings.WindDirection.y);
	const float speed = windLength > 1e-6f ? settings.WindSpeed / windLength : 0;
	mGpu.Wind = { settings.WindDirection.x * speed, settings.WindDirection.y * speed, settings.GustStrength, 0 };
	mGpu.PreviousCloudOffset = mGpu.CloudOffset;
	mGpu.CloudOffset.x += mGpu.Wind.x * settings.CloudWindMultiplier * dt;
	mGpu.CloudOffset.y += mGpu.Wind.y * settings.CloudWindMultiplier * dt;
	const float blend = settings.TransitionSeconds > 0 ? std::min(dt / settings.TransitionSeconds, 1.0f) : 1.0f;
	const float rain = settings.Precipitation == Weather::Rain ? settings.PrecipitationIntensity : 0;
	const float snow = settings.Precipitation == Weather::Snow ? settings.PrecipitationIntensity : 0;
	// Bounded linear ramps reach their target; a zero-duration transition is immediate.
	auto approach = [blend](float current, float target) { return current + std::clamp(target - current, -blend, blend); };
	mGpu.WeatherState.x = approach(mGpu.WeatherState.x, rain);
	mGpu.WeatherState.y = approach(mGpu.WeatherState.y, snow);
	mGpu.WeatherState.z = std::clamp(
		mGpu.WeatherState.z + (mGpu.WeatherState.x * settings.WetnessRate - settings.DryingRate * (1 - mGpu.WeatherState.x)) * dt, 0.0f, 1.0f);
	mGpu.WeatherState.w = std::clamp(
		mGpu.WeatherState.w + (mGpu.WeatherState.y * settings.SnowAccumulationRate - settings.SnowMeltRate * (1 - mGpu.WeatherState.y)) * dt, 0.0f,
		1.0f);
	mGpu.Timing = { float(mTime), dt, mHours, float(settings.Seed) };
}
const GpuState& Runtime::gpu() const {
	return mGpu;
}
float Runtime::timeOfDay() const {
	return mHours;
}
void Runtime::setTimeOfDay(float hours) {
	if (!std::isfinite(hours)) {
		throw std::invalid_argument("TimeOfDay must be finite");
	}
	mHours = std::fmod(std::fmod(hours, 24.0f) + 24.0f, 24.0f);
}
}
