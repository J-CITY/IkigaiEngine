#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace IKIGAI::RENDER {

// Why temporal history may have to be dropped. A reason is raised once per
// event; HistoryValidity maps it to the effects whose history it invalidates.
enum class HistoryReason : uint32_t {
	CameraCut,
	Resize,
	PipelineReload,
	LightChange,
	SceneTopology,
	SceneMaterial,
	SceneDynamicPose,
	EnvironmentChange,
	UserReset,
	ResourceUpdate,
	ProgressiveMotion,
	SceneLoading,
	Count
};

// Consumers of temporal history that are invalidated independently.
enum class HistoryEffect : uint32_t { GlobalIllumination, Reflections, Shadows, Presentation, Upscaler, Count };

constexpr size_t HistoryReasonCount = static_cast<size_t>(HistoryReason::Count);
constexpr size_t HistoryEffectCount = static_cast<size_t>(HistoryEffect::Count);

using HistoryEffectMask = uint32_t;

constexpr HistoryEffectMask historyEffectBit(HistoryEffect effect) {
	return 1u << static_cast<uint32_t>(effect);
}

constexpr HistoryEffectMask HistoryAllEffects = (1u << HistoryEffectCount) - 1u;

// Ray-traced effects that share the engine_Frame / engine_RayFrame history signal.
constexpr HistoryEffectMask HistoryRayTracedEffects
	= historyEffectBit(HistoryEffect::GlobalIllumination) | historyEffectBit(HistoryEffect::Reflections) | historyEffectBit(HistoryEffect::Shadows);
// Ray-traced effects plus the upscaler: dropped when a persistent pipeline resource changes.
constexpr HistoryEffectMask HistoryResourceEffects = HistoryRayTracedEffects | historyEffectBit(HistoryEffect::Upscaler);

const char* historyReasonName(HistoryReason reason);
const char* historyEffectName(HistoryEffect effect);
const char* historyEffectLogPrefix(HistoryEffect effect);
// Per-effect invalidation table. SceneDynamicPose is intentionally empty: it is
// a "scene moved" signal for denoisers and never drops accumulated history.
HistoryEffectMask historyEffectsInvalidatedBy(HistoryReason reason);

struct HistoryCounters {
	// Resets[effect][reason]: events that invalidated the effect.
	std::array<std::array<uint64_t, HistoryReasonCount>, HistoryEffectCount> Resets {};
	// Kept[effect][reason]: events that were raised but left the effect history intact.
	std::array<std::array<uint64_t, HistoryReasonCount>, HistoryEffectCount> Kept {};

	void add(const HistoryCounters& other);
	bool operator==(const HistoryCounters& other) const;
};

class HistoryValidity {
public:
	// Raises a reason: invalidates every effect in the reason mask and counts the event.
	void invalidate(HistoryReason reason);
	void invalidate(HistoryReason reason, HistoryEffectMask effects);
	// Marks effects valid again (their history was written by the frame that just finished).
	void markValid(HistoryEffectMask effects);

	bool isValid(HistoryEffect effect) const {
		return (mValid & historyEffectBit(effect)) != 0;
	}
	bool allValid(HistoryEffectMask effects) const {
		return (mValid & effects) == effects;
	}
	HistoryEffectMask validMask() const {
		return mValid;
	}
	// True when dynamic scene motion (skinning, rigid transforms) was raised this frame.
	bool sceneMoved() const {
		return mSceneMoved;
	}
	void beginFrame() {
		mSceneMoved = false;
	}

	const HistoryCounters& counters() const {
		return mCounters;
	}

private:
	HistoryEffectMask mValid = 0;
	bool mSceneMoved = false;
	HistoryCounters mCounters;
};

} // namespace IKIGAI::RENDER
