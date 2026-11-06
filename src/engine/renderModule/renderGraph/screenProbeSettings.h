#pragma once
#include <cstdint>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
// Visualisations written to the screen probe Indirect output instead of the irradiance. SOURCE colours the source of the
// probe rays (screen green, ray query blue, DDGI magenta, environment red, mixed by their share in the probe).
enum class ScreenProbeDebugMode { OFF = 0, PROBES, HIT_MISS, CONFIDENCE, RAW, FILTERED, SOURCE };

// Links of the screen probe trace chain screen -> ray query -> DDGI -> environment (hybrid-sp-rt). A link missing from
// ChainMask is skipped, so tests can force every fallback state.
enum class ScreenProbeChainLink { SCREEN = 0, RAY_QUERY, DDGI, ENVIRONMENT };

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
	// Trace screen misses with ray queries against the TLAS (stage with an RT program; bypassed without ray query support).
	bool UseRayQuery = true;
	// Read the DDGI volume of DdgiStage: secondary light at ray query hits and radiance beyond RayQueryMaxDistance.
	bool UseDdgi = false;
	// Zero uses the fixed grid. A positive value enables camera-relative DDGI and caps traced probes per frame.
	int DdgiProbeBudget = 0;
	// Range of the ray query trace in world units; longer rays continue in the DDGI volume, then the environment.
	float RayQueryMaxDistance = 30.0f;
	// Confidence of ray query samples (screen hits count 1, DDGI and environment samples count MissConfidence).
	float RayQueryConfidence = 0.9f;
	// Scale of the DDGI irradiance read by the screen probe chain.
	float DdgiIntensity = 1.0f;
	// RAY_TRACE stage that updates the DDGI volume; its push constants (OriginSpacing, CountsRays) define the probe grid.
	std::string DdgiStage;
	// Enabled links of the trace chain. A block without the field gets every link (serde default); the member starts empty
	// because serdepp appends list elements to the existing container.
	std::vector<ScreenProbeChainLink> ChainMask;
	// Textures sized one texel per probe (Scale = 1 / TileSize) and per probe direction (Scale = side / TileSize).
	std::vector<std::string> ProbeTextures;
	std::vector<std::string> AtlasTextures;
	// M4 temporal stack (probe history, shared full resolution denoiser). Stages in TemporalV2Stages run only when it is on,
	// stages in LegacyTemporalStages (the M3 SPTemporal) only when it is off; the inactive set is disabled at load and its
	// Bypass aliases rewire the graph. Debug visualisations (DebugMode other than OFF) always use the legacy stages.
	bool UseTemporalV2 = false;
	// Fix the sample position within each tile while temporal v2 is active.
	bool StableTemporalPlacement = false;
	std::vector<std::string> TemporalV2Stages;
	std::vector<std::string> LegacyTemporalStages;

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
			.field(&Self::UseRayQuery, "UseRayQuery", default_ { true })
			.field(&Self::UseDdgi, "UseDdgi", default_ { false })
			.field(&Self::DdgiProbeBudget, "DdgiProbeBudget", default_ { 0 })
			.field(&Self::RayQueryMaxDistance, "RayQueryMaxDistance", default_ { 30.0f })
			.field(&Self::RayQueryConfidence, "RayQueryConfidence", default_ { 0.9f })
			.field(&Self::DdgiIntensity, "DdgiIntensity", default_ { 1.0f })
			.field(&Self::DdgiStage, "DdgiStage", default_ { std::string {} })
			.field(&Self::ChainMask, "ChainMask",
				default_ { std::vector<ScreenProbeChainLink> {
				ScreenProbeChainLink::SCREEN, ScreenProbeChainLink::RAY_QUERY, ScreenProbeChainLink::DDGI, ScreenProbeChainLink::ENVIRONMENT } })
			.field(&Self::ProbeTextures, "ProbeTextures", default_ { std::vector<std::string> {} })
			.field(&Self::AtlasTextures, "AtlasTextures", default_ { std::vector<std::string> {} })
			.field(&Self::UseTemporalV2, "UseTemporalV2", default_ { false })
			.field(&Self::StableTemporalPlacement, "StableTemporalPlacement", default_ { false })
			.field(&Self::TemporalV2Stages, "TemporalV2Stages", default_ { std::vector<std::string> {} })
			.field(&Self::LegacyTemporalStages, "LegacyTemporalStages", default_ { std::vector<std::string> {} });
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
	// Bits of ScreenProbeChainBit; RAY_QUERY and DDGI are cleared when UseRayQuery / UseDdgi are off.
	int32_t ChainMask = 15;
	// 1 when the DDGI probe grid below was resolved from DdgiStage.
	int32_t DdgiValid = 0;
	int32_t StablePlacement = 0;
	int32_t DdgiScrolling = 0;
	float RayQueryMaxDistance = 30.0f;
	float RayQueryConfidence = 0.9f;
	float DdgiIntensity = 1.0f;
	float RayQueryReserved = 0.0f;
	float DdgiOriginX = 0.0f;
	float DdgiOriginY = 0.0f;
	float DdgiOriginZ = 0.0f;
	float DdgiSpacing = 1.0f;
	int32_t DdgiCountX = 0;
	int32_t DdgiCountY = 0;
	int32_t DdgiCountZ = 0;
	int32_t DdgiRays = 0;
};
static_assert(sizeof(ScreenProbeGpuSettings) == 128);

// Bit values of ScreenProbeGpuSettings::ChainMask (SP_CHAIN_* in screenProbeSettings.glsl).
enum ScreenProbeChainBit : int32_t {
	SCREEN_PROBE_CHAIN_SCREEN = 1,
	SCREEN_PROBE_CHAIN_RAY_QUERY = 2,
	SCREEN_PROBE_CHAIN_DDGI = 4,
	SCREEN_PROBE_CHAIN_ENVIRONMENT = 8
};

// DDGI probe grid decoded from the push constants of the ScreenProbes.DdgiStage stage (layout of ddgi.rgen: vec4 OriginSpacing,
// ivec4 CountsRays). Valid is false when UseDdgi is off.
struct ScreenProbeDdgiVolume {
	bool Valid = false;
	float OriginX = 0.0f;
	float OriginY = 0.0f;
	float OriginZ = 0.0f;
	float Spacing = 1.0f;
	int32_t CountX = 0;
	int32_t CountY = 0;
	int32_t CountZ = 0;
	int32_t Rays = 0;
};

// Pipeline uniform buffer that screen probe stages bind with BufferLinks { "ScreenProbe": ScreenProbeBufferName }.
inline constexpr const char* ScreenProbeBufferName = "ScreenProbeSettings";

// True when the M4 temporal stack runs: UseTemporalV2 and no debug visualisation (those are drawn by the legacy stage).
bool ScreenProbeTemporalV2Active(const ScreenProbeSettings& settings);
// Validates the parameters (throws std::runtime_error naming the field) and returns the shader block.
ScreenProbeGpuSettings ResolveScreenProbeGpuSettings(const ScreenProbeSettings& settings, const ScreenProbeDdgiVolume& volume);
// Decodes and validates a DDGI grid from RT push constant bytes (each element must be a byte 0..255).
ScreenProbeDdgiVolume DecodeScreenProbeDdgiVolume(const std::vector<int>& pushConstants, const std::string& stage);
}
