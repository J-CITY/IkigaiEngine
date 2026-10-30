#include "environmentEditor.h"
#ifdef USE_EDITOR
#include "coreModule/ecs/components/environmentComponent.h"
#include "coreModule/ecs/components/vegetationInteractorComponent.h"
#include <algorithm>
#include <array>
#include <imgui.h>

namespace IKIGAI::EDITOR {
void DrawEnvironmentEditor(ECS::EnvironmentComponent& component) {
	auto quality = component.quality();
	int tier = int(quality.Tier);
	bool qualityChanged = ImGui::Combo("Environment quality", &tier, "Custom\0Low\0Medium\0High\0");
	if (qualityChanged) {
		// Switching to Custom starts from the displayed tier's budgets.
		quality = quality.resolved();
		quality.Tier = ENVIRONMENT::QualityTier(tier);
	}
	if (quality.Tier == ENVIRONMENT::QualityTier::Custom && ImGui::TreeNode("Quality budgets")) {
		auto budget = [&](const char* label, unsigned& value, int minimum, int maximum) {
			int edited = int(value);
			if (ImGui::SliderInt(label, &edited, minimum, maximum)) {
				value = unsigned(edited);
				qualityChanged = true;
			}
		};
		budget("Cloud view steps", quality.CloudSteps, 8, 96);
		budget("Cloud light steps", quality.CloudLightSteps, 1, 6);
		budget("Cloud shadow steps", quality.CloudShadowSteps, 1, 8);
		budget("Cloud shadow interval (frames)", quality.CloudShadowUpdateFrames, 1, 120);
		budget("Maximum weather particles", quality.ParticleLimit, 0, 32768);
		budget("Maximum particle collision queries", quality.CollisionLimit, 0, 4096);
		budget("Maximum surface shelter queries", quality.SurfaceQueryLimit, 0, 4096);
		budget("Interaction capsule samples", quality.InteractionSamples, 0, 32);
		budget("Maximum fur shells", quality.FurShellLimit, 2, 32);
		qualityChanged |= ImGui::SliderFloat("Minimum IBL interval (seconds)", &quality.MinimumIblUpdateSeconds, 0, 30);
		ImGui::TreePop();
	}
	if (qualityChanged) {
		component.applyQuality(quality);
	}
	auto settings = component.effectiveSettings();
	bool changed = false;
	float hours = component.runtime().timeOfDay();
	if (ImGui::SliderFloat("Time of day (hours)", &hours, 0, 24, "%.2f")) {
		settings.TimeOfDay = hours;
		changed = true;
	}
	changed |= ImGui::Checkbox("Animate time", &settings.AnimateTime);
	changed |= ImGui::SliderFloat("Day duration (seconds)", &settings.DayDuration, 10, 3600, "%.0f", ImGuiSliderFlags_Logarithmic);
	changed |= ImGui::SliderFloat("Sun azimuth (degrees)", &settings.SunAzimuth, -180, 180);
	changed |= ImGui::SliderFloat("Sun intensity", &settings.SunIntensity, 0, 30);
	changed |= ImGui::SliderFloat("Turbidity", &settings.Turbidity, 1, 20);
	changed |= ImGui::SliderFloat("Atmosphere density", &settings.AtmosphereDensity, .01f, 3);
	changed |= ImGui::SliderFloat("Haze density", &settings.FogDensity, 0, .01f, "%.4f");
	changed |= ImGui::SliderFloat("Sky update interval (seconds)", &settings.AtmosphereUpdateSeconds, 0, 1, "%.2f");
	changed |= ImGui::SliderFloat("Sky sun direction threshold", &settings.AtmosphereSunThreshold, .0001f, .05f, "%.4f");
	changed |= ImGui::SliderFloat("Sky height threshold (world units)", &settings.AtmosphereHeightThreshold, .1f, 20, "%.1f");
	changed |= ImGui::Checkbox("Aerial perspective", &settings.AerialPerspective);
	int cloud = int(settings.Clouds);
	if (ImGui::Combo("Cloud type", &cloud, "Cumulus\0Stratus\0Storm\0")) {
		settings.Clouds = ENVIRONMENT::CloudType(cloud);
		changed = true;
	}
	changed |= ImGui::SliderFloat("Cloud coverage", &settings.CloudCoverage, 0, 1);
	changed |= ImGui::SliderFloat("Cloud density", &settings.CloudDensity, 0, .1f);
	changed |= ImGui::SliderFloat("Cloud darkness", &settings.CloudDarkness, 0, 1);
	changed |= ImGui::SliderFloat("Cloud base height", &settings.CloudBottom, 1, 1000);
	settings.CloudTop = std::max(settings.CloudTop, settings.CloudBottom + 1);
	changed |= ImGui::SliderFloat("Cloud top height", &settings.CloudTop, settings.CloudBottom + 1, settings.CloudBottom + 1000);
	changed |= ImGui::SliderFloat("Cloud shadows", &settings.CloudShadowStrength, 0, 1);
	changed |= ImGui::SliderFloat2("Wind direction X/Z", &settings.WindDirection.x, -1, 1);
	changed |= ImGui::SliderFloat("Wind speed", &settings.WindSpeed, 0, 30);
	changed |= ImGui::SliderFloat("Cloud wind multiplier", &settings.CloudWindMultiplier, 0, 10);
	changed |= ImGui::SliderFloat("Wind gusts", &settings.GustStrength, 0, 2);
	int weather = int(settings.Precipitation);
	if (ImGui::Combo("Precipitation", &weather, "Clear\0Rain\0Snow\0")) {
		settings.Precipitation = ENVIRONMENT::Weather(weather);
		changed = true;
	}
	changed |= ImGui::SliderFloat("Precipitation intensity", &settings.PrecipitationIntensity, 0, 1);
	changed |= ImGui::SliderFloat("Transition (seconds)", &settings.TransitionSeconds, 0, 30);
	changed |= ImGui::SliderFloat("Surface weather radius (world units)", &settings.SurfaceWeatherRadius, 1, 128);
	int surfaceQueries = int(settings.SurfaceWeatherQueryBudget);
	if (ImGui::SliderInt("Surface shelter queries per update", &surfaceQueries, 0, 256)) {
		settings.SurfaceWeatherQueryBudget = unsigned(surfaceQueries);
		changed = true;
	}
	int particles = int(settings.MaxParticles);
	if (ImGui::SliderInt("Particle budget", &particles, 0, 8192)) {
		settings.MaxParticles = unsigned(particles);
		changed = true;
	}
	if (changed) {
		component.apply(settings);
	}
	constexpr std::array<const char*, 6> presetLabels
		= { "Clear preset", "White clouds preset", "Overcast preset", "Storm preset", "Rain preset", "Snow preset" };
	for (size_t index = 0; index < presetLabels.size(); ++index) {
		if (index % 2 != 0) {
			ImGui::SameLine();
		}
		if (ImGui::Button(presetLabels[index])) {
			component.applyPreset(ENVIRONMENT::Preset(index + 1), settings.TransitionSeconds);
		}
	}
	nlohmann::json presetName = component.preset();
	ImGui::Text("Preset: %s", presetName.get<std::string>().c_str());
	if (component.presetProgress() < 1) {
		ImGui::ProgressBar(component.presetProgress(), ImVec2(-1, 0), "Environment transition");
	}
	const auto& state = component.runtime().gpu();
	ImGui::Text("Wetness %.2f, snow %.2f", state.WeatherState.z, state.WeatherState.w);
	ImGui::Text("Particles %zu, collision queries %u", component.weather().particles().size(), component.weather().queries());
	ImGui::Text("Surface shelter queries %u", component.surfaceWeather().queries());
}
void DrawVegetationInteractorEditor(ECS::VegetationInteractorComponent& component) {
	ImGui::SliderFloat("Interaction radius", &component.settings.Radius, .01f, 10);
	ImGui::SliderFloat("Capsule height", &component.settings.Height, 0, 10);
	ImGui::SliderFloat("Interaction strength", &component.settings.Strength, 0, 5);
	ImGui::SliderFloat("Recovery (seconds)", &component.settings.RecoverySeconds, 0, 5);
	ImGui::DragFloat3("Capsule offset", &component.settings.Offset.x, .01f);
}
}
#endif
