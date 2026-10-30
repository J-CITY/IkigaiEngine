#include "environmentQuality.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace IKIGAI::ENVIRONMENT {
namespace {
	constexpr std::array<std::string_view, 4> Names = { "Custom", "Low", "Medium", "High" };
}
void from_json(const nlohmann::json& json, QualityTier& value) {
	const auto name = json.get<std::string>();
	const auto found = std::find(Names.begin(), Names.end(), name);
	if (found == Names.end()) {
		throw std::invalid_argument("Unknown Environment quality tier: " + name);
	}
	value = QualityTier(found - Names.begin());
}
void to_json(nlohmann::json& json, const QualityTier& value) {
	if (size_t(value) >= Names.size()) {
		throw std::invalid_argument("Invalid Environment quality tier");
	}
	json = Names[size_t(value)];
}
void QualitySettings::validate() const {
	if (size_t(Tier) >= Names.size() || CloudSteps < 8 || CloudSteps > 96 || CloudLightSteps < 1 || CloudLightSteps > 6 || CloudShadowSteps < 1
		|| CloudShadowSteps > 8 || CloudShadowUpdateFrames < 1 || CloudShadowUpdateFrames > 120 || ParticleLimit > 32768 || CollisionLimit > 4096
		|| SurfaceQueryLimit > 4096 || InteractionSamples > 32 || FurShellLimit < 2 || FurShellLimit > 32 || !std::isfinite(MinimumIblUpdateSeconds)
		|| MinimumIblUpdateSeconds < 0 || MinimumIblUpdateSeconds > 3600) {
		throw std::invalid_argument("Environment quality budgets are out of range");
	}
}
QualitySettings QualitySettings::resolved() const {
	validate();
	if (Tier == QualityTier::Custom) {
		return *this;
	}
	QualitySettings result;
	result.Tier = Tier;
	switch (Tier) {
	case QualityTier::Low:
		result.CloudSteps = 24;
		result.CloudLightSteps = 2;
		result.CloudShadowSteps = 4;
		result.CloudShadowUpdateFrames = 4;
		result.ParticleLimit = 512;
		result.CollisionLimit = 16;
		result.SurfaceQueryLimit = 4;
		result.InteractionSamples = 8;
		result.FurShellLimit = 8;
		result.MinimumIblUpdateSeconds = 4;
		break;
	case QualityTier::Medium:
		result.CloudSteps = 48;
		result.CloudLightSteps = 4;
		result.CloudShadowSteps = 6;
		result.CloudShadowUpdateFrames = 2;
		result.ParticleLimit = 2048;
		result.CollisionLimit = 64;
		result.SurfaceQueryLimit = 16;
		result.InteractionSamples = 16;
		result.FurShellLimit = 16;
		result.MinimumIblUpdateSeconds = 2;
		break;
	case QualityTier::High:
		result.ParticleLimit = 8192;
		result.CollisionLimit = 128;
		result.SurfaceQueryLimit = 32;
		result.MinimumIblUpdateSeconds = .5f;
		break;
	default:
		break;
	}
	return result;
}
Settings QualitySettings::limit(const Settings& settings) const {
	const auto budgets = resolved();
	auto result = settings;
	result.MaxParticles = std::min(result.MaxParticles, budgets.ParticleLimit);
	result.CollisionBudget = std::min(result.CollisionBudget, budgets.CollisionLimit);
	result.SurfaceWeatherQueryBudget = std::min(result.SurfaceWeatherQueryBudget, budgets.SurfaceQueryLimit);
	result.IblUpdateSeconds = std::max(result.IblUpdateSeconds, budgets.MinimumIblUpdateSeconds);
	return result;
}
}
