#include "surfaceMaterial.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/materialInterface.h"
#include "renderModule/render.h"
#include "resourceModule/serviceManager.h"
#include "surfaceCoverage.h"
#include <stdexcept>

namespace IKIGAI::SURFACE {
void validateSurfaceMaterials(const Settings& settings, const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& sources) {
	if (sources.size() < settings.Layers.size()) {
		throw std::invalid_argument("Missing surface coating sources");
	}
	for (unsigned index = 0; index < settings.Layers.size(); ++index) {
		if (!sources[index]) {
			throw std::invalid_argument("Smooth surface coatings require a material asset");
		}
		const auto source = sources[index]->getDescriptor();
		if (source.PbrLayers.Layers.size() > 1 || source.PbrLayers.AlphaMode != RENDER::PbrAlphaMode::Opaque
			|| source.Deformation.Mode != RENDER::DeformationMode::None) {
			throw std::invalid_argument("Surface coatings require opaque, undeformed PBR materials with at most one layer");
		}
		if (!source.PbrLayers.Layers.empty()) {
			const auto& layer = source.PbrLayers.Layers.front();
			if (!layer.Enabled || layer.Weight != 1 || !layer.MaskMap.empty() || layer.InvertMask) {
				throw std::invalid_argument("Surface coating source layer must have full, unmasked coverage");
			}
		}
		else {
			for (const auto* name : { "u_MetallicMap", "u_RoughnessMap", "u_AmbientOcclusionMap" }) {
				if (source.Uniforms.contains(name)) {
					throw std::invalid_argument("Use a PBR coating with packed ORM instead of separate legacy factor maps");
				}
			}
		}
	}
}
std::shared_ptr<RENDER::MaterialInterface> buildSurfaceMaterial(
	const Settings& settings, const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& sources, const std::vector<uint8_t>* coveragePixels) {
	RENDER::MaterialResource descriptor;
	descriptor.path = "/__surface_blended__";
	descriptor.NeedFileWatch = false;
	descriptor.ShaderPath = "/shaders/render/portable/vegetation.shader";
	descriptor.IsDeferred = true;
	bool baseOrm = false;
	bool baseHeight = false;
	float baseAo = 1.0f;
	float baseNormalStrength = 1.0f;
	for (unsigned index = 0; index < settings.Layers.size(); ++index) {
		if (!sources[index]) {
			throw std::invalid_argument("Smooth surface coatings require a material asset");
		}
		const auto source = sources[index]->getDescriptor();
		if (source.PbrLayers.Layers.size() > 1 || source.PbrLayers.AlphaMode != RENDER::PbrAlphaMode::Opaque
			|| source.Deformation.Mode != RENDER::DeformationMode::None) {
			throw std::invalid_argument("Surface coatings require opaque, undeformed PBR materials with at most one layer");
		}
		RENDER::PbrLayerDescriptor layer;
		if (!source.PbrLayers.Layers.empty()) {
			layer = source.PbrLayers.Layers.front();
			if (!layer.Enabled || layer.Weight != 1 || !layer.MaskMap.empty() || layer.InvertMask) {
				throw std::invalid_argument("Surface coating source layer must have full, unmasked coverage");
			}
		}
		else {
			// Convert the legacy base surface to one reusable coating.
			auto read = [&](const char* name, auto& target) {
				if (const auto found = source.Uniforms.find(name); found != source.Uniforms.end()) {
					if (const auto value = std::get_if<std::decay_t<decltype(target)>>(&found->second)) {
						target = *value;
					}
				}
			};
			read("u_AlbedoMap", layer.AlbedoMap);
			read("u_NormalMap", layer.NormalMap);
			read("u_HeightMap", layer.HeightMap);
			read("data_UBOu_Albedo", layer.Color);
			read("data_UBOu_Roughness", layer.Roughness);
			read("data_UBOu_Metallic", layer.Metallic);
			read("data_UBOu_TextureTiling", layer.Tiling);
			read("data_UBOu_TextureOffset", layer.Offset);
			for (const auto* unsupported : { "u_MetallicMap", "u_RoughnessMap", "u_AmbientOcclusionMap" }) {
				if (source.Uniforms.contains(unsupported)) {
					throw std::invalid_argument("Use a PbrLayers coating with packed ORM instead of separate legacy factor maps");
				}
			}
		}
		layer.Tiling *= settings.Layers[index].TextureScale;
		if (index == 0) {
			descriptor.Uniforms["data_UBOu_Albedo"] = layer.Color;
			descriptor.Uniforms["data_UBOu_Roughness"] = layer.Roughness;
			descriptor.Uniforms["data_UBOu_Metallic"] = layer.Metallic;
			descriptor.Uniforms["data_UBOu_TextureTiling"] = layer.Tiling;
			descriptor.Uniforms["data_UBOu_TextureOffset"] = layer.Offset;
			descriptor.Uniforms["data_UBOu_EnableNormalMapping"] = !layer.NormalMap.empty();
			if (!layer.AlbedoMap.empty()) {
				descriptor.Uniforms["u_AlbedoMap"] = layer.AlbedoMap;
			}
			if (!layer.NormalMap.empty()) {
				descriptor.Uniforms["u_NormalMap"] = layer.NormalMap;
			}
			if (!layer.HeightMap.empty()) {
				descriptor.Uniforms["u_HeightMap"] = layer.HeightMap;
			}
			if (!layer.OrmMap.empty()) {
				descriptor.Uniforms["u_RoughnessMap"] = layer.OrmMap;
				descriptor.Uniforms["u_MetallicMap"] = layer.OrmMap;
				descriptor.Uniforms["u_AmbientOcclusionMap"] = layer.OrmMap;
				baseOrm = true;
			}
			baseHeight = !layer.HeightMap.empty();
			baseAo = layer.AmbientOcclusion;
			baseNormalStrength = layer.NormalStrength;
			descriptor.PbrLayers.BaseHeightScale = layer.HeightScale;
			descriptor.PbrLayers.BaseHeightBias = layer.HeightBias;
		}
		else {
			layer.MaskMap.clear();
			layer.MaskChannel = RENDER::PbrMaskChannel(index - 1);
			const auto uv = coverageUv(settings);
			layer.MaskTiling = { uv.x, uv.y };
			layer.MaskOffset = { uv.z, uv.w };
			layer.BlendMode = RENDER::PbrBlendMode::Height;
			layer.HeightInfluence = settings.Blend.HeightInfluence;
			layer.TransitionWidth = settings.Blend.TransitionWidth;
			descriptor.PbrLayers.Layers.push_back(layer);
		}
	}
	auto& renderer = RESOURCES::ServiceManager::Get<RENDER::Renderer>();
	auto result = renderer.createMaterial(descriptor);
	RENDER::TextureResource mask;
	mask.path = "/__surface_coverage__";
	mask.width = int(settings.Blend.MapResolution);
	mask.height = int(settings.Blend.MapResolution);
	mask.depth = 1;
	mask.channels = 4;
	// Preserve CPU/GPU coverage agreement; filtered masks have no gamma conversion.
	mask.useMipmap = false;
	mask.colorData = coveragePixels ? *coveragePixels : buildCoverageMap(settings);
	auto texture = renderer.createTexture(mask);
	result->set("pbr_LayersBase", MATH::Vector4f { baseHeight ? 1.0f : 0.0f, baseOrm ? 1.0f : 0.0f, baseAo, baseNormalStrength });
	for (unsigned index = 0; index < descriptor.PbrLayers.Layers.size(); ++index) {
		const auto prefix = "pbr_LayersLayer" + std::to_string(index);
		auto flags = std::get<MATH::Vector4f>(result->get(prefix + "Height"));
		flags.z = float(int(flags.z) | 16);
		result->set(prefix + "Height", flags);
		result->set("pbr_Layer" + std::to_string(index) + "MaskMap", texture);
	}
	return result;
}
}
