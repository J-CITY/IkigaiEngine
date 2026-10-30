#pragma once
#include "component.h"
#include "coreModule/ecs/componentReference.h"
#include "directionalLight.h"
#include "environmentModule/environment.h"
#include "environmentModule/environmentPresets.h"
#include "environmentModule/environmentQuality.h"
#include "environmentModule/surfaceWeather.h"
#include "environmentModule/weather.h"

namespace IKIGAI::ECS {
class EnvironmentComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = {};
		ComponentReference Sun = {};
		ENVIRONMENT::Settings Environment = {};
		ENVIRONMENT::Preset Preset = ENVIRONMENT::Preset::Custom;
		ENVIRONMENT::QualitySettings Quality = {};
		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "EnvironmentComponentType")
				.field(&Descriptor::Sun, "Sun", default_ { ComponentReference {} })
				.field(&Descriptor::Environment, "Environment", default_ { ENVIRONMENT::Settings {} })
				.field(&Descriptor::Preset, "Preset", default_ { ENVIRONMENT::Preset::Custom })
				.field(&Descriptor::Quality, "Quality", default_ { ENVIRONMENT::QualitySettings {} });
		}
	};
	explicit EnvironmentComponent(UTILS::Ref<Object> object);
	EnvironmentComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void apply(const ENVIRONMENT::Settings& value);
	void applyPreset(ENVIRONMENT::Preset preset, float seconds);
	ENVIRONMENT::Preset preset() const;
	float presetProgress() const;
	const ENVIRONMENT::Settings& effectiveSettings() const;
	ENVIRONMENT::Settings simulationSettings() const;
	const ENVIRONMENT::QualitySettings& quality() const;
	void applyQuality(const ENVIRONMENT::QualitySettings& quality);
	const ENVIRONMENT::Settings& settings() const;
	int sunObjectId() const;
	ENVIRONMENT::Runtime& runtime();
	ENVIRONMENT::WeatherRuntime& weather();
	ENVIRONMENT::SurfaceWeatherRuntime& surfaceWeather();
	void update(SCENE_SYSTEM::Scene& scene, double dt);
	void onStart() override;

private:
	Descriptor mDescriptor = {};
	ENVIRONMENT::Runtime mRuntime = {};
	ENVIRONMENT::PresetTransition mPresetTransition = {};
	ENVIRONMENT::WeatherRuntime mWeather = {};
	ENVIRONMENT::SurfaceWeatherRuntime mSurfaceWeather = {};
	UTILS::WeakPtr<DirectionalLight> mSun = {};
};
template <> inline std::string GetType<EnvironmentComponent>() {
	return "class IKIGAI::ECS::EnvironmentComponent";
}
template <> inline std::string GetComponentName<EnvironmentComponent>() {
	return "EnvironmentComponent";
}
}
