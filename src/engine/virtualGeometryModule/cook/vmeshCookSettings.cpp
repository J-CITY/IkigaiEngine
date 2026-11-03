#include "vmeshCookSettings.h"

#include <cmath>
#include <stdexcept>

#include "virtualGeometryModule/format/vmeshIO.h"

namespace IKIGAI::VG {
namespace {
	void Require(bool condition, const char* message) {
		if (!condition) {
			throw std::invalid_argument(std::string("VMeshCookSettings: ") + message);
		}
	}

	bool InOpenUnitRange(float value) {
		return std::isfinite(value) && value > 0.0f && value < 1.0f;
	}
}

void ValidateCookSettings(const VMeshCookSettings& settings) {
	Require(
		settings.ClusterMaxTriangles >= 4u && settings.ClusterMaxTriangles <= VMeshMaxClusterTriangles, "ClusterMaxTriangles must be in [4, 128]");
	Require(settings.ClusterMaxVertices >= 3u && settings.ClusterMaxVertices <= 256u, "ClusterMaxVertices must be in [3, 256]");
	Require(std::isfinite(settings.ClusterMinTriangleFraction) && settings.ClusterMinTriangleFraction > 0.0f
			&& settings.ClusterMinTriangleFraction <= 1.0f,
		"ClusterMinTriangleFraction must be in (0, 1]");
	Require(std::isfinite(settings.ConeWeight) && settings.ConeWeight >= 0.0f && settings.ConeWeight <= 1.0f, "ConeWeight must be in [0, 1]");
	Require(settings.GroupSize >= 2u && settings.GroupSize <= 64u, "GroupSize must be in [2, 64]");
	Require(InOpenUnitRange(settings.SimplifyRatio), "SimplifyRatio must be in (0, 1)");
	Require(InOpenUnitRange(settings.SimplifyStuckThreshold), "SimplifyStuckThreshold must be in (0, 1)");
	Require(settings.SimplifyStuckThreshold > settings.SimplifyRatio, "SimplifyStuckThreshold must be greater than SimplifyRatio");
	Require(std::isfinite(settings.NormalWeight) && settings.NormalWeight >= 0.0f, "NormalWeight must be finite and non-negative");
	Require(std::isfinite(settings.UvWeight) && settings.UvWeight >= 0.0f, "UvWeight must be finite and non-negative");
	Require(std::isfinite(settings.SloppyErrorFactor) && settings.SloppyErrorFactor >= 1.0f, "SloppyErrorFactor must be >= 1");
	Require(InOpenUnitRange(settings.FallbackTriangleRatio) || settings.FallbackTriangleRatio == 1.0f, "FallbackTriangleRatio must be in (0, 1]");
	Require(settings.MinFallbackTriangles >= 1u, "MinFallbackTriangles must be at least 1");
	Require(
		std::isfinite(settings.WeldPositionEpsilon) && settings.WeldPositionEpsilon >= 0.0f, "WeldPositionEpsilon must be finite and non-negative");
}

std::string SerializeCookSettings(const VMeshCookSettings& settings) {
	return serde::serialize<nlohmann::json>(settings).dump();
}

uint64_t HashCookSettings(const VMeshCookSettings& settings) {
	const std::string json = SerializeCookSettings(settings);
	return HashVMeshBytes(json.data(), json.size());
}

VMeshCookSettings ParseCookSettings(const std::string& json) {
	nlohmann::json parsed = nlohmann::json::parse(json, nullptr, true, true);
	VMeshCookSettings settings = serde::deserialize<VMeshCookSettings>(parsed);
	ValidateCookSettings(settings);
	return settings;
}
}
