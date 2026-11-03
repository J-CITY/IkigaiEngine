#include "screenProbeSettings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	void Require(bool condition, const std::string& field, const std::string& rule) {
		if (!condition) {
			throw std::runtime_error("ScreenProbes." + field + " " + rule);
		}
	}

	bool Finite(float value) {
		return std::isfinite(value);
	}

	int DirectionSide(int directions) {
		const int side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(std::max(directions, 0)))));
		return side * side == directions ? side : 0;
	}

	int32_t ChainBit(ScreenProbeChainLink link) {
		switch (link) {
		case ScreenProbeChainLink::SCREEN:
			return SCREEN_PROBE_CHAIN_SCREEN;
		case ScreenProbeChainLink::RAY_QUERY:
			return SCREEN_PROBE_CHAIN_RAY_QUERY;
		case ScreenProbeChainLink::DDGI:
			return SCREEN_PROBE_CHAIN_DDGI;
		case ScreenProbeChainLink::ENVIRONMENT:
			return SCREEN_PROBE_CHAIN_ENVIRONMENT;
		}
		return 0;
	}

	template <class T> T ReadPushValue(const std::vector<uint8_t>& bytes, size_t offset) {
		T value {};
		std::memcpy(&value, bytes.data() + offset, sizeof(T));
		return value;
	}
}

ScreenProbeDdgiVolume DecodeScreenProbeDdgiVolume(const std::vector<int>& pushConstants, const std::string& stage) {
	const std::string field = "DdgiStage";
	// ddgi.rgen: vec4 OriginSpacing (origin xyz, spacing) followed by ivec4 CountsRays (probe counts xyz, rays per probe).
	constexpr size_t gridBytes = 32;
	Require(pushConstants.size() >= gridBytes, field, "stage " + stage + " needs at least 32 push constant bytes (OriginSpacing, CountsRays)");
	std::vector<uint8_t> bytes;
	bytes.reserve(gridBytes);
	for (size_t i = 0; i < gridBytes; ++i) {
		Require(pushConstants[i] >= 0 && pushConstants[i] <= 255, field, "stage " + stage + " push constants must be bytes");
		bytes.push_back(static_cast<uint8_t>(pushConstants[i]));
	}
	ScreenProbeDdgiVolume volume;
	volume.OriginX = ReadPushValue<float>(bytes, 0);
	volume.OriginY = ReadPushValue<float>(bytes, 4);
	volume.OriginZ = ReadPushValue<float>(bytes, 8);
	volume.Spacing = ReadPushValue<float>(bytes, 12);
	volume.CountX = ReadPushValue<int32_t>(bytes, 16);
	volume.CountY = ReadPushValue<int32_t>(bytes, 20);
	volume.CountZ = ReadPushValue<int32_t>(bytes, 24);
	volume.Rays = ReadPushValue<int32_t>(bytes, 28);
	Require(Finite(volume.OriginX) && Finite(volume.OriginY) && Finite(volume.OriginZ), field, "stage " + stage + " has a non-finite grid origin");
	Require(Finite(volume.Spacing) && volume.Spacing > 0, field, "stage " + stage + " needs a finite positive probe spacing");
	Require(volume.CountX >= 2 && volume.CountY >= 2 && volume.CountZ >= 2 && volume.CountX <= 64 && volume.CountY <= 64 && volume.CountZ <= 64,
		field, "stage " + stage + " probe counts must be in [2, 64]");
	Require(volume.Rays >= 1 && volume.Rays <= 1024, field, "stage " + stage + " rays per probe must be in [1, 1024]");
	volume.Valid = true;
	return volume;
}

bool ScreenProbeTemporalV2Active(const ScreenProbeSettings& settings) {
	return settings.UseTemporalV2 && settings.DebugMode == ScreenProbeDebugMode::OFF;
}

ScreenProbeGpuSettings ResolveScreenProbeGpuSettings(const ScreenProbeSettings& settings, const ScreenProbeDdgiVolume& volume) {
	Require(settings.TileSize >= 4 && settings.TileSize <= 64, "TileSize", "must be in [4, 64]");
	const int side = DirectionSide(settings.DirectionsPerProbe);
	Require(side >= 2 && side <= 16, "DirectionsPerProbe", "must be a square side^2 with side in [2, 16]");
	Require(settings.MaxSteps >= 1 && settings.MaxSteps <= 512, "MaxSteps", "must be in [1, 512]");
	Require(Finite(settings.Thickness) && settings.Thickness > 0, "Thickness", "must be finite and positive");
	Require(Finite(settings.Intensity) && settings.Intensity >= 0, "Intensity", "must be finite and non-negative");
	Require(Finite(settings.MissConfidence) && settings.MissConfidence >= 0 && settings.MissConfidence <= 1, "MissConfidence", "must be in [0, 1]");
	Require(Finite(settings.PreviousSceneWeight) && settings.PreviousSceneWeight >= 0 && settings.PreviousSceneWeight <= 1, "PreviousSceneWeight",
		"must be in [0, 1]");
	Require(settings.MaxHistory >= 1 && settings.MaxHistory <= 256, "MaxHistory", "must be in [1, 256]");
	Require(settings.SpatialRadius >= 0 && settings.SpatialRadius <= 3, "SpatialRadius", "must be in [0, 3]");
	Require(settings.HzbMaxLevel >= 0 && settings.HzbMaxLevel <= 15, "HzbMaxLevel", "must be in [0, 15]");
	Require(Finite(settings.DepthSigma) && settings.DepthSigma > 0, "DepthSigma", "must be finite and positive");
	Require(Finite(settings.NormalPower) && settings.NormalPower > 0, "NormalPower", "must be finite and positive");
	Require(Finite(settings.ClampSigma) && settings.ClampSigma >= 0, "ClampSigma", "must be finite and non-negative");
	Require(Finite(settings.ConfidenceHistory) && settings.ConfidenceHistory >= 1, "ConfidenceHistory", "must be finite and >= 1");
	Require(Finite(settings.RayQueryMaxDistance) && settings.RayQueryMaxDistance > 0, "RayQueryMaxDistance", "must be finite and positive");
	Require(Finite(settings.RayQueryConfidence) && settings.RayQueryConfidence >= 0 && settings.RayQueryConfidence <= 1, "RayQueryConfidence",
		"must be in [0, 1]");
	Require(Finite(settings.DdgiIntensity) && settings.DdgiIntensity >= 0, "DdgiIntensity", "must be finite and non-negative");
	Require(!settings.UseDdgi || volume.Valid, "UseDdgi", "requires a resolved DdgiStage volume");
	int32_t chainMask = 0;
	for (const auto link : settings.ChainMask) {
		const auto bit = ChainBit(link);
		Require(bit != 0 && (chainMask & bit) == 0, "ChainMask", "must list each link (SCREEN, RAY_QUERY, DDGI, ENVIRONMENT) at most once");
		chainMask |= bit;
	}
	if (!settings.UseRayQuery) {
		chainMask &= ~SCREEN_PROBE_CHAIN_RAY_QUERY;
	}
	if (!settings.UseDdgi) {
		chainMask &= ~SCREEN_PROBE_CHAIN_DDGI;
	}

	ScreenProbeGpuSettings gpu;
	gpu.TileSize = settings.TileSize;
	gpu.DirectionSide = side;
	gpu.MaxSteps = settings.MaxSteps;
	gpu.DebugMode = static_cast<int32_t>(settings.DebugMode);
	gpu.Thickness = settings.Thickness;
	gpu.Intensity = settings.Intensity;
	gpu.MissConfidence = settings.MissConfidence;
	gpu.PreviousSceneWeight = settings.PreviousSceneWeight;
	gpu.UseHistory = settings.UseHistory ? 1 : 0;
	gpu.MaxHistory = settings.MaxHistory;
	gpu.SpatialRadius = settings.SpatialRadius;
	gpu.HzbMaxLevel = settings.HzbMaxLevel;
	gpu.DepthSigma = settings.DepthSigma;
	gpu.NormalPower = settings.NormalPower;
	gpu.ClampSigma = settings.ClampSigma;
	gpu.ConfidenceHistory = settings.ConfidenceHistory;
	gpu.ChainMask = chainMask;
	gpu.RayQueryMaxDistance = settings.RayQueryMaxDistance;
	gpu.RayQueryConfidence = settings.RayQueryConfidence;
	gpu.DdgiIntensity = settings.DdgiIntensity;
	if (settings.UseDdgi && volume.Valid) {
		gpu.DdgiValid = 1;
		gpu.DdgiOriginX = volume.OriginX;
		gpu.DdgiOriginY = volume.OriginY;
		gpu.DdgiOriginZ = volume.OriginZ;
		gpu.DdgiSpacing = volume.Spacing;
		gpu.DdgiCountX = volume.CountX;
		gpu.DdgiCountY = volume.CountY;
		gpu.DdgiCountZ = volume.CountZ;
		gpu.DdgiRays = volume.Rays;
	}
	return gpu;
}
}
