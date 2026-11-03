#pragma once

#include "mathModule/math.h"
#include <nlohmann/json.hpp>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {

enum class PbrBlendMode { Linear, Height };
enum class PbrMaskChannel { R, G, B, A };
enum class PbrNormalMode { Replace, Detail };
enum class PbrAlphaMode { Opaque, Masked, Blend };

void from_json(const nlohmann::json& json, PbrBlendMode& value);
void to_json(nlohmann::json& json, const PbrBlendMode& value);
void from_json(const nlohmann::json& json, PbrMaskChannel& value);
void to_json(nlohmann::json& json, const PbrMaskChannel& value);
void from_json(const nlohmann::json& json, PbrNormalMode& value);
void to_json(nlohmann::json& json, const PbrNormalMode& value);
void from_json(const nlohmann::json& json, PbrAlphaMode& value);
void to_json(nlohmann::json& json, const PbrAlphaMode& value);

// A layer is surface data owned by one material, never a separate draw/material.
struct PbrLayerDescriptor {
	bool Enabled = true;
	MATH::Vector4f Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	float Metallic = 0.0f;
	float Roughness = 0.5f;
	float AmbientOcclusion = 1.0f;
	float NormalStrength = 1.0f;
	MATH::Vector2f Tiling = { 1.0f, 1.0f };
	MATH::Vector2f Offset = { 0.0f, 0.0f };
	std::string AlbedoMap;
	std::string NormalMap;
	std::string OrmMap;
	std::string HeightMap;
	std::string MaskMap;
	MATH::Vector2f MaskTiling = { 1.0f, 1.0f };
	MATH::Vector2f MaskOffset = { 0.0f, 0.0f };
	PbrMaskChannel MaskChannel = PbrMaskChannel::R;
	bool InvertMask = false;
	float Weight = 1.0f;
	PbrBlendMode BlendMode = PbrBlendMode::Linear;
	PbrNormalMode NormalMode = PbrNormalMode::Replace;
	float HeightScale = 1.0f;
	float HeightBias = 0.0f;
	float HeightInfluence = 1.0f;
	float TransitionWidth = 0.2f;

	template <class Context> static void serde(Context& context, PbrLayerDescriptor& value) {
		using Self = PbrLayerDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Enabled, "Enabled", default_ { true })
			.field(&Self::Color, "Color", default_ { MATH::Vector4f { 1.0f, 1.0f, 1.0f, 1.0f } })
			.field(&Self::Metallic, "Metallic", default_ { 0.0f })
			.field(&Self::Roughness, "Roughness", default_ { 0.5f })
			.field(&Self::AmbientOcclusion, "AmbientOcclusion", default_ { 1.0f })
			.field(&Self::NormalStrength, "NormalStrength", default_ { 1.0f })
			.field(&Self::Tiling, "Tiling", default_ { MATH::Vector2f { 1.0f, 1.0f } })
			.field(&Self::Offset, "Offset", default_ { MATH::Vector2f { 0.0f, 0.0f } })
			.field(&Self::AlbedoMap, "AlbedoMap", default_ { std::string {} })
			.field(&Self::NormalMap, "NormalMap", default_ { std::string {} })
			.field(&Self::OrmMap, "OrmMap", default_ { std::string {} })
			.field(&Self::HeightMap, "HeightMap", default_ { std::string {} })
			.field(&Self::MaskMap, "MaskMap", default_ { std::string {} })
			.field(&Self::MaskTiling, "MaskTiling", default_ { MATH::Vector2f { 1.0f, 1.0f } })
			.field(&Self::MaskOffset, "MaskOffset", default_ { MATH::Vector2f { 0.0f, 0.0f } })
			.field(&Self::MaskChannel, "MaskChannel", default_ { PbrMaskChannel::R })
			.field(&Self::InvertMask, "InvertMask", default_ { false })
			.field(&Self::Weight, "Weight", default_ { 1.0f })
			.field(&Self::BlendMode, "BlendMode", default_ { PbrBlendMode::Linear })
			.field(&Self::NormalMode, "NormalMode", default_ { PbrNormalMode::Replace })
			.field(&Self::HeightScale, "HeightScale", default_ { 1.0f })
			.field(&Self::HeightBias, "HeightBias", default_ { 0.0f })
			.field(&Self::HeightInfluence, "HeightInfluence", default_ { 1.0f })
			.field(&Self::TransitionWidth, "TransitionWidth", default_ { 0.2f });
	}
};

struct PbrLayersDescriptor {
	unsigned Version = 1;
	PbrAlphaMode AlphaMode = PbrAlphaMode::Opaque;
	float AlphaCutoff = 0.5f;
	float BaseHeightScale = 1.0f;
	float BaseHeightBias = 0.0f;
	std::vector<PbrLayerDescriptor> Layers;

	void validate(const std::string& materialPath) const;
	bool requiresLayeredShader() const;

	template <class Context> static void serde(Context& context, PbrLayersDescriptor& value) {
		using Self = PbrLayersDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Version, "Version", default_ { 1u })
			.field(&Self::AlphaMode, "AlphaMode", default_ { PbrAlphaMode::Opaque })
			.field(&Self::AlphaCutoff, "AlphaCutoff", default_ { 0.5f })
			.field(&Self::BaseHeightScale, "BaseHeightScale", default_ { 1.0f })
			.field(&Self::BaseHeightBias, "BaseHeightBias", default_ { 0.0f })
			.field(&Self::Layers, "Layers", default_ { std::vector<PbrLayerDescriptor> {} });
	}
};

// Emissive surface radiance: Color is linear RGB (0..1), Intensity scales it into HDR and may exceed 1 (radiance = Color * Intensity).
// Raster passes receive it through the `u_EmissiveColor` uniform of shaders that declare it; ray tracing reads it from the descriptor.
struct PbrEmissiveDescriptor {
	MATH::Vector3f Color = { 1.0f, 1.0f, 1.0f };
	float Intensity = 0.0f;

	void validate(const std::string& materialPath) const;
	bool isActive() const;
	MATH::Vector3f radiance() const;

	template <class Context> static void serde(Context& context, PbrEmissiveDescriptor& value) {
		using Self = PbrEmissiveDescriptor;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Color, "Color", default_ { MATH::Vector3f { 1.0f, 1.0f, 1.0f } })
			.field(&Self::Intensity, "Intensity", default_ { 0.0f });
	}
};

// Also used by CPU probes: the shader implements the same endpoint-preserving rule.
float PbrHeightBlendWeight(float weight, float baseHeight, float layerHeight, float influence, float transitionWidth);

} // namespace IKIGAI::RENDER
