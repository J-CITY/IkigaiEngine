#include "pbrMaterialDescriptor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace IKIGAI::RENDER {
namespace {
	template <class Enum, size_t Size> void readEnum(const nlohmann::json& json, Enum& value, const std::array<std::string_view, Size>& names) {
		const auto name = json.get<std::string>();
		const auto found = std::find(names.begin(), names.end(), name);
		if (found == names.end()) {
			throw std::invalid_argument("Unknown PBR enum: " + name);
		}
		value = static_cast<Enum>(std::distance(names.begin(), found));
	}
	constexpr std::array<std::string_view, 2> blendNames { "Linear", "Height" };
	constexpr std::array<std::string_view, 4> channelNames { "R", "G", "B", "A" };
	constexpr std::array<std::string_view, 2> normalNames { "Replace", "Detail" };
	constexpr std::array<std::string_view, 3> alphaNames { "Opaque", "Masked", "Blend" };

	void requireRange(float value, float low, float high, const std::string& context) {
		if (!std::isfinite(value) || value < low || value > high) {
			throw std::invalid_argument(context + ": value out of range");
		}
	}
	void requireUv(const MATH::Vector2f& tiling, const MATH::Vector2f& offset, const std::string& context) {
		if (!std::isfinite(tiling.x) || !std::isfinite(tiling.y) || tiling.x <= 0 || tiling.y <= 0 || !std::isfinite(offset.x)
			|| !std::isfinite(offset.y)) {
			throw std::invalid_argument(context + ": finite UVs and positive tiling required");
		}
	}
} // namespace

void from_json(const nlohmann::json& json, PbrBlendMode& value) {
	readEnum(json, value, blendNames);
}
void to_json(nlohmann::json& json, const PbrBlendMode& value) {
	json = blendNames.at(static_cast<size_t>(value));
}
void from_json(const nlohmann::json& json, PbrMaskChannel& value) {
	readEnum(json, value, channelNames);
}
void to_json(nlohmann::json& json, const PbrMaskChannel& value) {
	json = channelNames.at(static_cast<size_t>(value));
}
void from_json(const nlohmann::json& json, PbrNormalMode& value) {
	readEnum(json, value, normalNames);
}
void to_json(nlohmann::json& json, const PbrNormalMode& value) {
	json = normalNames.at(static_cast<size_t>(value));
}
void from_json(const nlohmann::json& json, PbrAlphaMode& value) {
	readEnum(json, value, alphaNames);
}
void to_json(nlohmann::json& json, const PbrAlphaMode& value) {
	json = alphaNames.at(static_cast<size_t>(value));
}

void PbrLayersDescriptor::validate(const std::string& materialPath) const {
	const auto context = "PbrLayers in " + materialPath;
	if (Version != 1) {
		throw std::invalid_argument(context + ": unsupported Version");
	}
	if (Layers.size() > 3) {
		throw std::invalid_argument(context + ": at most three additional layers are supported");
	}
	requireRange(AlphaCutoff, 0.0f, 1.0f, context + ".AlphaCutoff");
	requireRange(BaseHeightScale, 0.0f, 65536.0f, context + ".BaseHeightScale");
	requireRange(BaseHeightBias, -65536.0f, 65536.0f, context + ".BaseHeightBias");
	for (size_t index = 0; index < Layers.size(); ++index) {
		const auto& layer = Layers[index];
		const auto label = context + ".Layers[" + std::to_string(index) + "]";
		requireRange(layer.Color.x, 0.0f, 1.0f, label + ".Color.X");
		requireRange(layer.Color.y, 0.0f, 1.0f, label + ".Color.Y");
		requireRange(layer.Color.z, 0.0f, 1.0f, label + ".Color.Z");
		requireRange(layer.Color.w, 0.0f, 1.0f, label + ".Color.W");
		requireRange(layer.Metallic, 0.0f, 1.0f, label + ".Metallic");
		requireRange(layer.Roughness, 0.0f, 1.0f, label + ".Roughness");
		requireRange(layer.AmbientOcclusion, 0.0f, 1.0f, label + ".AmbientOcclusion");
		requireRange(layer.NormalStrength, 0.0f, 16.0f, label + ".NormalStrength");
		requireRange(layer.Weight, 0.0f, 1.0f, label + ".Weight");
		requireRange(layer.HeightScale, 0.0f, 65536.0f, label + ".HeightScale");
		requireRange(layer.HeightBias, -65536.0f, 65536.0f, label + ".HeightBias");
		requireRange(layer.HeightInfluence, 0.0f, 16.0f, label + ".HeightInfluence");
		requireRange(layer.TransitionWidth, 0.0001f, 1.0f, label + ".TransitionWidth");
		requireUv(layer.Tiling, layer.Offset, label);
		requireUv(layer.MaskTiling, layer.MaskOffset, label + ".Mask");
	}
}

void PbrEmissiveDescriptor::validate(const std::string& materialPath) const {
	const auto context = "Emissive in " + materialPath;
	requireRange(Color.x, 0.0f, 1.0f, context + ".Color.X");
	requireRange(Color.y, 0.0f, 1.0f, context + ".Color.Y");
	requireRange(Color.z, 0.0f, 1.0f, context + ".Color.Z");
	// RGBA16F G-buffer targets saturate at 65504.
	requireRange(Intensity, 0.0f, 65504.0f, context + ".Intensity");
}

bool PbrEmissiveDescriptor::isActive() const {
	return Intensity > 0.0f;
}

MATH::Vector3f PbrEmissiveDescriptor::radiance() const {
	return isActive() ? MATH::Vector3f { Color.x * Intensity, Color.y * Intensity, Color.z * Intensity } : MATH::Vector3f { 0.0f, 0.0f, 0.0f };
}

bool PbrLayersDescriptor::requiresLayeredShader() const {
	return !Layers.empty() || AlphaMode != PbrAlphaMode::Opaque || AlphaCutoff != 0.5f || BaseHeightScale != 1.0f || BaseHeightBias != 0.0f;
}

float PbrHeightBlendWeight(float weight, float baseHeight, float layerHeight, float influence, float transitionWidth) {
	const auto clampedWeight = std::clamp(weight, 0.0f, 1.0f);
	if (clampedWeight <= 0.0f || clampedWeight >= 1.0f || influence == 0.0f) {
		return clampedWeight;
	}
	const auto shifted = clampedWeight + 4.0f * clampedWeight * (1.0f - clampedWeight) * influence * (layerHeight - baseHeight);
	const auto width = std::max(transitionWidth, 0.0001f);
	const auto normalized = std::clamp((shifted - (0.5f - width * 0.5f)) / width, 0.0f, 1.0f);
	return normalized * normalized * (3.0f - 2.0f * normalized);
}
} // namespace IKIGAI::RENDER
