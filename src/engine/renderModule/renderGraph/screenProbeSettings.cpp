#include "screenProbeSettings.h"

#include <algorithm>
#include <cmath>
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
}

ScreenProbeGpuSettings ResolveScreenProbeGpuSettings(const ScreenProbeSettings& settings) {
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
	return gpu;
}
}
