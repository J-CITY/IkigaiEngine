#pragma once
#include "environment.h"

namespace IKIGAI::ENVIRONMENT {
enum class QualityTier { Custom, Low, Medium, High };
void from_json(const nlohmann::json& json, QualityTier& value);
void to_json(nlohmann::json& json, const QualityTier& value);

struct QualitySettings {
	QualityTier Tier = QualityTier::Custom;
	unsigned CloudSteps = 96;
	unsigned CloudLightSteps = 6;
	unsigned CloudShadowSteps = 8;
	unsigned CloudShadowUpdateFrames = 1;
	unsigned ParticleLimit = 32768;
	unsigned CollisionLimit = 4096;
	unsigned SurfaceQueryLimit = 4096;
	unsigned InteractionSamples = 32;
	unsigned FurShellLimit = 32;
	float MinimumIblUpdateSeconds = 0.0f;
	void validate() const;
	QualitySettings resolved() const;
	Settings limit(const Settings& settings) const;
	bool operator==(const QualitySettings&) const = default;
	template <class C> static void serde(C& context, QualitySettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&QualitySettings::Tier, "Tier", default_ { QualityTier::Custom })
			.field(&QualitySettings::CloudSteps, "CloudSteps", default_ { 96u })
			.field(&QualitySettings::CloudLightSteps, "CloudLightSteps", default_ { 6u })
			.field(&QualitySettings::CloudShadowSteps, "CloudShadowSteps", default_ { 8u })
			.field(&QualitySettings::CloudShadowUpdateFrames, "CloudShadowUpdateFrames", default_ { 1u })
			.field(&QualitySettings::ParticleLimit, "ParticleLimit", default_ { 32768u })
			.field(&QualitySettings::CollisionLimit, "CollisionLimit", default_ { 4096u })
			.field(&QualitySettings::SurfaceQueryLimit, "SurfaceQueryLimit", default_ { 4096u })
			.field(&QualitySettings::InteractionSamples, "InteractionSamples", default_ { 32u })
			.field(&QualitySettings::FurShellLimit, "FurShellLimit", default_ { 32u })
			.field(&QualitySettings::MinimumIblUpdateSeconds, "MinimumIblUpdateSeconds", default_ { 0.0f });
	}
};
}
