#include "temporalDenoiseSettings.h"

#include <cmath>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	void Require(bool condition, const TemporalDenoiseSettings& settings, const std::string& field, const std::string& rule) {
		if (!condition) {
			throw std::runtime_error("TemporalDenoisers[" + settings.Name + "]." + field + " " + rule);
		}
	}

	bool Finite(float value) {
		return std::isfinite(value);
	}
}

TemporalDenoiseGpuSettings ResolveTemporalDenoiseGpuSettings(const TemporalDenoiseSettings& settings) {
	Require(!settings.Name.empty(), settings, "Name", "must not be empty");
	Require(settings.MaxHistory >= 1 && settings.MaxHistory <= 256, settings, "MaxHistory", "must be in [1, 256]");
	Require(Finite(settings.ClampSigma) && settings.ClampSigma >= 0 && settings.ClampSigma <= 16, settings, "ClampSigma", "must be in [0, 16]");
	Require(settings.ClampStride >= 1 && settings.ClampStride <= 64, settings, "ClampStride", "must be in [1, 64]");
	Require(Finite(settings.DepthTolerance) && settings.DepthTolerance > 0 && settings.DepthTolerance <= 1, settings, "DepthTolerance",
		"must be in (0, 1]");
	Require(Finite(settings.DepthBias) && settings.DepthBias >= 0, settings, "DepthBias", "must be finite and non-negative");
	Require(Finite(settings.NormalThreshold) && settings.NormalThreshold >= -1 && settings.NormalThreshold <= 1, settings, "NormalThreshold",
		"must be in [-1, 1]");
	Require(Finite(settings.ConfidenceHistory) && settings.ConfidenceHistory >= 1, settings, "ConfidenceHistory", "must be finite and >= 1");
	Require(Finite(settings.AntiLagSigma) && settings.AntiLagSigma >= 0, settings, "AntiLagSigma", "must be finite and non-negative (0 = off)");
	Require(Finite(settings.FastHistory) && settings.FastHistory >= 1 && settings.FastHistory <= static_cast<float>(settings.MaxHistory), settings,
		"FastHistory", "must be in [1, MaxHistory]");

	TemporalDenoiseGpuSettings gpu;
	gpu.MaxHistory = settings.MaxHistory;
	gpu.HistoryValid = 0;
	gpu.ClampStride = settings.ClampStride;
	gpu.Effect = static_cast<int32_t>(settings.Effect);
	gpu.DepthTolerance = settings.DepthTolerance;
	gpu.NormalThreshold = settings.NormalThreshold;
	gpu.ConfidenceHistory = settings.ConfidenceHistory;
	gpu.DepthBias = settings.DepthBias;
	gpu.ClampSigma = settings.ClampSigma;
	gpu.AntiLagSigma = settings.AntiLagSigma;
	gpu.FastHistory = settings.FastHistory;
	return gpu;
}
}
