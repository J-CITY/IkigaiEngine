#pragma once
#include <cstdint>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
// Visualisations written to the screen probe Indirect output instead of the irradiance.
enum class ScreenProbeDebugMode { OFF = 0, PROBES, HIT_MISS, CONFIDENCE, RAW, FILTERED };

// Pipeline "ScreenProbes" block (hybrid-sp): parameters of the compute screen probe gather. Validated at pipeline load
// (ResolveScreenProbes); the resolved values reach the stages through the uniform buffer ScreenProbeBufferName.
struct ScreenProbeSettings {
	bool Enabled = false;
	// Pixels per probe tile side (one probe per TileSize x TileSize pixels).
	int TileSize = 16;
	// Ray directions per probe; must be a square (side x side atlas cells per probe).
	int DirectionsPerProbe = 64;
	// Iterations of the hierarchical screen march per ray.
	int MaxSteps = 48;
	// Accepted depth of a hit behind the screen surface, in view-space units.
	float Thickness = 0.25f;
	// Scale of the gathered irradiance.
	float Intensity = 1.0f;
	bool UseHistory = true;
	ScreenProbeDebugMode DebugMode = ScreenProbeDebugMode::OFF;
	// Confidence of rays that fall back to the environment (hits count 1).
	float MissConfidence = 0.5f;
	// Blend of the previous HDR frame into hit radiance (multi-bounce); 0 uses the lit scene of this frame only.
	float PreviousSceneWeight = 0.0f;
	int MaxHistory = 32;
	// Probe neighbourhood radius of the spatial filter (0 disables it).
	int SpatialRadius = 1;
	// Coarsest closest-depth pyramid mip the march may use.
	int HzbMaxLevel = 6;
	// Plane-distance tolerance of the bilateral weights, relative to the view depth.
	float DepthSigma = 0.05f;
	// Exponent of the normal similarity weight.
	float NormalPower = 8.0f;
	// Variance clamp width of the temporal history, in standard deviations.
	float ClampSigma = 1.5f;
	// History length at which the output confidence reaches the probe validity.
	float ConfidenceHistory = 4.0f;
	// Textures sized one texel per probe (Scale = 1 / TileSize) and per probe direction (Scale = side / TileSize).
	std::vector<std::string> ProbeTextures;
	std::vector<std::string> AtlasTextures;

	template <class Context> constexpr static auto serde(Context& context, ScreenProbeSettings& value) {
		using Self = ScreenProbeSettings;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Enabled, "Enabled", default_ { false })
			.field(&Self::TileSize, "TileSize", default_ { 16 })
			.field(&Self::DirectionsPerProbe, "DirectionsPerProbe", default_ { 64 })
			.field(&Self::MaxSteps, "MaxSteps", default_ { 48 })
			.field(&Self::Thickness, "Thickness", default_ { 0.25f })
			.field(&Self::Intensity, "Intensity", default_ { 1.0f })
			.field(&Self::UseHistory, "UseHistory", default_ { true })
			.field(&Self::DebugMode, "DebugMode", default_ { ScreenProbeDebugMode::OFF })
			.field(&Self::MissConfidence, "MissConfidence", default_ { 0.5f })
			.field(&Self::PreviousSceneWeight, "PreviousSceneWeight", default_ { 0.0f })
			.field(&Self::MaxHistory, "MaxHistory", default_ { 32 })
			.field(&Self::SpatialRadius, "SpatialRadius", default_ { 1 })
			.field(&Self::HzbMaxLevel, "HzbMaxLevel", default_ { 6 })
			.field(&Self::DepthSigma, "DepthSigma", default_ { 0.05f })
			.field(&Self::NormalPower, "NormalPower", default_ { 8.0f })
			.field(&Self::ClampSigma, "ClampSigma", default_ { 1.5f })
			.field(&Self::ConfidenceHistory, "ConfidenceHistory", default_ { 4.0f })
			.field(&Self::ProbeTextures, "ProbeTextures", default_ { std::vector<std::string> {} })
			.field(&Self::AtlasTextures, "AtlasTextures", default_ { std::vector<std::string> {} });
	}
};

// std140 image of ScreenProbeSettingsBlock (shaders/indirectLighting/screenProbes/screenProbeCommon.glsl).
struct ScreenProbeGpuSettings {
	int32_t TileSize = 16;
	int32_t DirectionSide = 8;
	int32_t MaxSteps = 48;
	int32_t DebugMode = 0;
	float Thickness = 0.25f;
	float Intensity = 1.0f;
	float MissConfidence = 0.5f;
	float PreviousSceneWeight = 0.0f;
	int32_t UseHistory = 1;
	int32_t MaxHistory = 32;
	int32_t SpatialRadius = 1;
	int32_t HzbMaxLevel = 6;
	float DepthSigma = 0.05f;
	float NormalPower = 8.0f;
	float ClampSigma = 1.5f;
	float ConfidenceHistory = 4.0f;
};
static_assert(sizeof(ScreenProbeGpuSettings) == 64);

// Pipeline uniform buffer that screen probe stages bind with BufferLinks { "ScreenProbe": ScreenProbeBufferName }.
inline constexpr const char* ScreenProbeBufferName = "ScreenProbeSettings";

// Validates the parameters (throws std::runtime_error naming the field) and returns the shader block.
ScreenProbeGpuSettings ResolveScreenProbeGpuSettings(const ScreenProbeSettings& settings);
}
