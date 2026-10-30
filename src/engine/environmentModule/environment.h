#pragma once
#include "mathModule/math.h"
#include <array>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/serializer.hpp>

namespace IKIGAI::ENVIRONMENT {
enum class Weather { Clear, Rain, Snow };
enum class CloudType { Cumulus, Stratus, Storm };
void from_json(const nlohmann::json& json, Weather& value);
void to_json(nlohmann::json& json, const Weather& value);
void from_json(const nlohmann::json& json, CloudType& value);
void to_json(nlohmann::json& json, const CloudType& value);

struct Settings {
	float TimeOfDay = 12.0f;
	float DayDuration = 1200.0f;
	bool AnimateTime = false;
	float SunAzimuth = 30.0f;
	float SunIntensity = 8.0f;
	float SunAngularRadius = 0.00465f;
	float Turbidity = 2.0f;
	float AtmosphereDensity = 1.0f;
	float FogDensity = 0.0003f;
	CloudType Clouds = CloudType::Cumulus;
	float CloudCoverage = 0.6f;
	float CloudDensity = 0.025f;
	float CloudBottom = 80.0f;
	float CloudTop = 160.0f;
	float CloudDarkness = 0.0f;
	float CloudShadowStrength = 0.75f;
	MATH::Vector2f WindDirection = { 1.0f, 0.2f };
	float WindSpeed = 1.5f;
	float CloudWindMultiplier = 1.0f;
	float GustStrength = 0.3f;
	Weather Precipitation = Weather::Clear;
	float PrecipitationIntensity = 0.7f;
	float TransitionSeconds = 4.0f;
	unsigned MaxParticles = 2048;
	float PrecipitationRadius = 18.0f;
	float PrecipitationHeight = 22.0f;
	float RainSpeed = 18.0f;
	float SnowSpeed = 1.5f;
	unsigned CollisionBudget = 64;
	float SurfaceWeatherRadius = 24.0f;
	unsigned SurfaceWeatherQueryBudget = 16;
	float WetnessRate = 0.08f;
	float DryingRate = 0.015f;
	float SnowAccumulationRate = 0.03f;
	float SnowMeltRate = 0.01f;
	float IblUpdateSeconds = 2.0f;
	float IblSunThreshold = 0.025f;
	float AtmosphereUpdateSeconds = 0.1f;
	float AtmosphereSunThreshold = 0.002f;
	float AtmosphereHeightThreshold = 2.0f;
	bool AerialPerspective = true;
	unsigned Seed = 17;
	void validate() const;
	template <class C> static void serde(C& context, Settings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Settings::TimeOfDay, "TimeOfDay", default_ { 12.0f })
			.field(&Settings::DayDuration, "DayDuration", default_ { 1200.0f })
			.field(&Settings::AnimateTime, "AnimateTime", default_ { false })
			.field(&Settings::SunAzimuth, "SunAzimuth", default_ { 30.0f })
			.field(&Settings::SunIntensity, "SunIntensity", default_ { 8.0f })
			.field(&Settings::SunAngularRadius, "SunAngularRadius", default_ { 0.00465f })
			.field(&Settings::Turbidity, "Turbidity", default_ { 2.0f })
			.field(&Settings::AtmosphereDensity, "AtmosphereDensity", default_ { 1.0f })
			.field(&Settings::FogDensity, "FogDensity", default_ { 0.0003f })
			.field(&Settings::Clouds, "Clouds", default_ { CloudType::Cumulus })
			.field(&Settings::CloudCoverage, "CloudCoverage", default_ { 0.6f })
			.field(&Settings::CloudDensity, "CloudDensity", default_ { 0.025f })
			.field(&Settings::CloudBottom, "CloudBottom", default_ { 80.0f })
			.field(&Settings::CloudTop, "CloudTop", default_ { 160.0f })
			.field(&Settings::CloudDarkness, "CloudDarkness", default_ { 0.0f })
			.field(&Settings::CloudShadowStrength, "CloudShadowStrength", default_ { 0.75f })
			.field(&Settings::WindDirection, "WindDirection", default_ { MATH::Vector2f { 1.0f, 0.2f } })
			.field(&Settings::WindSpeed, "WindSpeed", default_ { 1.5f })
			.field(&Settings::CloudWindMultiplier, "CloudWindMultiplier", default_ { 1.0f })
			.field(&Settings::GustStrength, "GustStrength", default_ { 0.3f })
			.field(&Settings::Precipitation, "Precipitation", default_ { Weather::Clear })
			.field(&Settings::PrecipitationIntensity, "PrecipitationIntensity", default_ { 0.7f })
			.field(&Settings::TransitionSeconds, "TransitionSeconds", default_ { 4.0f })
			.field(&Settings::MaxParticles, "MaxParticles", default_ { 2048u })
			.field(&Settings::PrecipitationRadius, "PrecipitationRadius", default_ { 18.0f })
			.field(&Settings::PrecipitationHeight, "PrecipitationHeight", default_ { 22.0f })
			.field(&Settings::RainSpeed, "RainSpeed", default_ { 18.0f })
			.field(&Settings::SnowSpeed, "SnowSpeed", default_ { 1.5f })
			.field(&Settings::CollisionBudget, "CollisionBudget", default_ { 64u })
			.field(&Settings::SurfaceWeatherRadius, "SurfaceWeatherRadius", default_ { 24.0f })
			.field(&Settings::SurfaceWeatherQueryBudget, "SurfaceWeatherQueryBudget", default_ { 16u })
			.field(&Settings::WetnessRate, "WetnessRate", default_ { 0.08f })
			.field(&Settings::DryingRate, "DryingRate", default_ { 0.015f })
			.field(&Settings::SnowAccumulationRate, "SnowAccumulationRate", default_ { 0.03f })
			.field(&Settings::SnowMeltRate, "SnowMeltRate", default_ { 0.01f })
			.field(&Settings::IblUpdateSeconds, "IblUpdateSeconds", default_ { 2.0f })
			.field(&Settings::IblSunThreshold, "IblSunThreshold", default_ { 0.025f })
			.field(&Settings::AtmosphereUpdateSeconds, "AtmosphereUpdateSeconds", default_ { 0.1f })
			.field(&Settings::AtmosphereSunThreshold, "AtmosphereSunThreshold", default_ { 0.002f })
			.field(&Settings::AtmosphereHeightThreshold, "AtmosphereHeightThreshold", default_ { 2.0f })
			.field(&Settings::AerialPerspective, "AerialPerspective", default_ { true })
			.field(&Settings::Seed, "Seed", default_ { 17u });
	}
};

// std140; independent of the stable EngineUBO/FrameData contracts.
struct alignas(16) GpuState {
	MATH::Vector4f SunDirection = {};
	MATH::Vector4f SunColor = {};
	MATH::Vector4f Atmosphere = {};
	MATH::Vector4f CloudLayer = {};
	MATH::Vector4f CloudShape = {};
	MATH::Vector4f Wind = {}; // velocity xz, gust strength, resolved sun light index + 1 (zero means absent)
	MATH::Vector4f CloudOffset = {}; // advection xy; renderer fills view/light step caps in zw
	MATH::Vector4f PreviousCloudOffset = {}; // previous advection xy; renderer fills shadow step cap in z
	MATH::Vector4f WeatherState = {};
	MATH::Vector4f Timing = {};
	MATH::Vector4f CloudShadowMap = {};
	MATH::Vector4f AtmosphereLut = {};
};
static_assert(sizeof(GpuState) == 192);

class Runtime {
public:
	void update(const Settings& settings, double seconds);
	const GpuState& gpu() const;
	float timeOfDay() const;
	void setTimeOfDay(float hours);

private:
	GpuState mGpu = {};
	double mTime = 0.0;
	float mHours = 12.0f;
	float mAuthoredHours = -1.0f;
};
}
