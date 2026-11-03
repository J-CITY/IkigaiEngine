#include "historyValidity.h"

namespace IKIGAI::RENDER {

namespace {
	constexpr HistoryEffectMask GlobalIlluminationAndReflections
		= historyEffectBit(HistoryEffect::GlobalIllumination) | historyEffectBit(HistoryEffect::Reflections);
	constexpr HistoryEffectMask EnvironmentEffects = HistoryResourceEffects | historyEffectBit(HistoryEffect::Presentation);
}

const char* historyReasonName(HistoryReason reason) {
	switch (reason) {
	case HistoryReason::CameraCut:
		return "CameraCut";
	case HistoryReason::Resize:
		return "Resize";
	case HistoryReason::PipelineReload:
		return "PipelineReload";
	case HistoryReason::LightChange:
		return "LightChange";
	case HistoryReason::SceneTopology:
		return "SceneTopology";
	case HistoryReason::SceneMaterial:
		return "SceneMaterial";
	case HistoryReason::SceneDynamicPose:
		return "SceneDynamicPose";
	case HistoryReason::EnvironmentChange:
		return "EnvironmentChange";
	case HistoryReason::UserReset:
		return "UserReset";
	case HistoryReason::ResourceUpdate:
		return "ResourceUpdate";
	case HistoryReason::ProgressiveMotion:
		return "ProgressiveMotion";
	case HistoryReason::SceneLoading:
		return "SceneLoading";
	case HistoryReason::Count:
		break;
	}
	return "Unknown";
}

const char* historyEffectName(HistoryEffect effect) {
	switch (effect) {
	case HistoryEffect::GlobalIllumination:
		return "GlobalIllumination";
	case HistoryEffect::Reflections:
		return "Reflections";
	case HistoryEffect::Shadows:
		return "Shadows";
	case HistoryEffect::Presentation:
		return "Presentation";
	case HistoryEffect::Upscaler:
		return "Upscaler";
	case HistoryEffect::Count:
		break;
	}
	return "Unknown";
}

const char* historyEffectLogPrefix(HistoryEffect effect) {
	switch (effect) {
	case HistoryEffect::GlobalIllumination:
		return "GI";
	case HistoryEffect::Reflections:
		return "REFLECTION";
	case HistoryEffect::Shadows:
		return "SHADOW";
	case HistoryEffect::Presentation:
		return "PRESENTATION";
	case HistoryEffect::Upscaler:
		return "UPSCALER";
	case HistoryEffect::Count:
		break;
	}
	return "UNKNOWN";
}

HistoryEffectMask historyEffectsInvalidatedBy(HistoryReason reason) {
	switch (reason) {
	case HistoryReason::CameraCut:
	case HistoryReason::Resize:
	case HistoryReason::PipelineReload:
	case HistoryReason::UserReset:
	case HistoryReason::SceneLoading:
		return HistoryAllEffects;
	case HistoryReason::LightChange:
	case HistoryReason::SceneTopology:
	case HistoryReason::ProgressiveMotion:
		return HistoryRayTracedEffects;
	case HistoryReason::SceneMaterial:
		// Surface response changed; shadow visibility did not.
		return GlobalIlluminationAndReflections;
	case HistoryReason::EnvironmentChange:
		return EnvironmentEffects;
	case HistoryReason::ResourceUpdate:
		return HistoryResourceEffects;
	case HistoryReason::SceneDynamicPose:
	case HistoryReason::Count:
		break;
	}
	return 0;
}

void HistoryCounters::add(const HistoryCounters& other) {
	for (size_t effect = 0; effect < HistoryEffectCount; ++effect) {
		for (size_t reason = 0; reason < HistoryReasonCount; ++reason) {
			Resets[effect][reason] += other.Resets[effect][reason];
			Kept[effect][reason] += other.Kept[effect][reason];
		}
	}
}

bool HistoryCounters::operator==(const HistoryCounters& other) const {
	return Resets == other.Resets && Kept == other.Kept;
}

void HistoryValidity::invalidate(HistoryReason reason) {
	invalidate(reason, historyEffectsInvalidatedBy(reason));
}

void HistoryValidity::invalidate(HistoryReason reason, HistoryEffectMask effects) {
	if (reason == HistoryReason::SceneDynamicPose) {
		mSceneMoved = true;
	}
	const auto reasonIndex = static_cast<size_t>(reason);
	for (size_t effect = 0; effect < HistoryEffectCount; ++effect) {
		if ((effects & (1u << effect)) != 0) {
			mValid &= ~(1u << effect);
			++mCounters.Resets[effect][reasonIndex];
		}
		else {
			++mCounters.Kept[effect][reasonIndex];
		}
	}
}

void HistoryValidity::markValid(HistoryEffectMask effects) {
	mValid |= effects;
}

} // namespace IKIGAI::RENDER
