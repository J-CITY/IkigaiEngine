#pragma once
#include <cstdint>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
// Effect whose per-effect history validity (HistoryValidity, M3-1) gates the denoiser history.
enum class TemporalDenoiseEffect { GLOBAL_ILLUMINATION = 0, REFLECTIONS };

// One entry of the pipeline "TemporalDenoisers" list: parameters of a temporal filter stage (shaders including
// indirectLighting/denoise/temporalCommon.glsl). The entry becomes a uniform buffer named Name that stages bind with
// BufferLinks { "Denoise": Name }; its HistoryValid field is rewritten every frame. Validated at pipeline load
// (ResolveTemporalDenoisers). The defaults are the values the hybrid-sp / hybrid-sp-rt full resolution pass ships with.
struct TemporalDenoiseSettings {
	std::string Name;
	TemporalDenoiseEffect Effect = TemporalDenoiseEffect::GLOBAL_ILLUMINATION;
	// Cap of the history length; alpha = 1 / history length.
	int MaxHistory = 32;
	// Variance clamp width in standard deviations of the current neighbourhood (0 disables the clamp).
	float ClampSigma = 1.5f;
	// Distance in texels between the 3x3 clamp taps of the full resolution pass (screen probes: one tile).
	int ClampStride = 1;
	// Disocclusion: accepted linear depth difference = DepthTolerance x depth + DepthBias.
	float DepthTolerance = 0.03f;
	float DepthBias = 0.01f;
	// Disocclusion: minimum cosine between the current and the previous normal.
	float NormalThreshold = 0.9f;
	// History length at which the output confidence reaches the accumulated validity.
	float ConfidenceHistory = 4.0f;
	// Anti-lag: history drops to FastHistory frames when the fast average differs from the history by more than
	// AntiLagSigma of its expected noise (0 disables it).
	float AntiLagSigma = 0.0f;
	float FastHistory = 4.0f;

	template <class Context> constexpr static auto serde(Context& context, TemporalDenoiseSettings& value) {
		using Self = TemporalDenoiseSettings;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Name, "Name")
			.field(&Self::Effect, "Effect", default_ { TemporalDenoiseEffect::GLOBAL_ILLUMINATION })
			.field(&Self::MaxHistory, "MaxHistory", default_ { 32 })
			.field(&Self::ClampSigma, "ClampSigma", default_ { 1.5f })
			.field(&Self::ClampStride, "ClampStride", default_ { 1 })
			.field(&Self::DepthTolerance, "DepthTolerance", default_ { 0.03f })
			.field(&Self::DepthBias, "DepthBias", default_ { 0.01f })
			.field(&Self::NormalThreshold, "NormalThreshold", default_ { 0.9f })
			.field(&Self::ConfidenceHistory, "ConfidenceHistory", default_ { 4.0f })
			.field(&Self::AntiLagSigma, "AntiLagSigma", default_ { 0.0f })
			.field(&Self::FastHistory, "FastHistory", default_ { 4.0f });
	}
};

// std140 image of TemporalDenoiseSettingsBlock (shaders/indirectLighting/denoise/temporalDenoiseSettings.glsl).
struct TemporalDenoiseGpuSettings {
	int32_t MaxHistory = 32;
	int32_t HistoryValid = 0;
	int32_t ClampStride = 1;
	int32_t Effect = 0;
	float DepthTolerance = 0.03f;
	float NormalThreshold = 0.9f;
	float ConfidenceHistory = 4.0f;
	float DepthBias = 0.01f;
	float ClampSigma = 1.5f;
	float AntiLagSigma = 0.0f;
	float FastHistory = 4.0f;
	float Reserved = 0.0f;
};
static_assert(sizeof(TemporalDenoiseGpuSettings) == 48);

// Per-pipeline runtime state of the temporal denoisers (RenderGraphPipeline::mTemporalDenoise).
struct TemporalDenoiseRuntime {
	struct Entry {
		std::string Name;
		TemporalDenoiseEffect Effect = TemporalDenoiseEffect::GLOBAL_ILLUMINATION;
		TemporalDenoiseGpuSettings Gpu;
		bool Uploaded = false;
	};
	std::vector<Entry> Entries;
	// Hash of the scene light data of the previous frame (DetectTemporalDenoiseLightChange).
	uint64_t LightHash = 0;
	bool LightHashValid = false;
};

// Validates the parameters (throws std::runtime_error naming the entry and field) and returns the shader block with
// HistoryValid = 0.
TemporalDenoiseGpuSettings ResolveTemporalDenoiseGpuSettings(const TemporalDenoiseSettings& settings);
}
