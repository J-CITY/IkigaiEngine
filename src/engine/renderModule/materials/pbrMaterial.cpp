#include "renderModule/backends/interface/materialInterface.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/textureManager.h"
#include <cmath>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	constexpr auto fallbackPath = "/__pbr_neutral_white__";

	bool reflected(const ShaderInterface& shader, const std::string& name) {
		for (const auto& uniform : shader.getReflection().mUniforms) {
			if (uniform.mName == name) {
				return true;
			}
			for (const auto& member : uniform.mMembers) {
				if (uniform.mName + member.mName == name) {
					return true;
				}
			}
		}
		return false;
	}
} // namespace

MaterialInterface::PreparedMaterial MaterialInterface::prepareMaterial(const MaterialResource& resource) {
	resource.Deformation.validate();
	resource.PbrLayers.validate(resource.path);
	auto& shaders = RESOURCES::ServiceManager::Get<RESOURCES::ShaderLoader>();
	auto& textures = RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>();
	auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	for (size_t index = 0; index < resource.PbrLayers.Layers.size(); ++index) {
		const auto& layer = resource.PbrLayers.Layers[index];
		for (const auto* path : { &layer.AlbedoMap, &layer.NormalMap, &layer.OrmMap, &layer.HeightMap, &layer.MaskMap }) {
			if (!path->empty() && !files.isFileExist(*path)) {
				throw std::invalid_argument("Missing PBR texture in " + resource.path + ".Layers[" + std::to_string(index) + "]: " + *path);
			}
		}
	}
	PreparedMaterial result;
	result.shader = shaders.loadMaterialShader(resource.ShaderPath, static_cast<unsigned>(resource.PbrLayers.Layers.size()));
	const auto layered = result.shader->supportsPbrLayers();
	if (!layered && resource.PbrLayers.requiresLayeredShader()) {
		throw std::invalid_argument("PbrLayers requires a compatible shader: " + resource.path);
	}
	const auto loadTexture = [&](const std::string& path, const std::string& label) {
		if (layered && (path.empty() || !files.isFileExist(path))) {
			throw std::invalid_argument("Missing PBR texture " + label + " in " + resource.path + ": " + path);
		}
		auto texture = path.ends_with(".texture") ? textures.createFromResource(path) : textures.createFromFile(path, true);
		if (layered && (!texture || texture->getType() != TextureType::TEXTURE_2D)) {
			throw std::invalid_argument("PBR requires a 2D texture " + label + " in " + resource.path);
		}
		return texture;
	};
	for (const auto& [name, value] : resource.Uniforms) {
		if (layered && name.starts_with("pbr_")) {
			throw std::invalid_argument("Reserved generated PBR uniform in " + resource.path + ": " + name);
		}
		std::visit(
			[&](const auto& entry) {
				using Value = std::decay_t<decltype(entry)>;
				if constexpr (std::is_same_v<Value, std::string>) {
					result.uniforms[name] = loadTexture(entry, name);
				}
				else {
					result.uniforms[name] = entry;
				}
			},
			value);
	}
	if (!layered) {
		return result;
	}
	// Reject mismatched base uniforms before the backend's raw UBO upload.
	for (const auto& uniform : result.shader->getReflection().mUniforms) {
		for (const auto& member : uniform.mMembers) {
			const auto key = uniform.mName + member.mName;
			const auto entry = result.uniforms.find(key);
			if (entry == result.uniforms.end()) {
				continue;
			}
			auto& value = entry->second;
			using Type = ShaderReflection::UniformType;
			if (member.mType == Type::FLOAT) {
				if (const auto* integer = std::get_if<int>(&value)) {
					value = static_cast<float>(*integer);
				}
			}
			// SPIR-V lowers boolean block members to 32-bit integers.
			if (member.mType == Type::INT) {
				if (const auto* boolean = std::get_if<bool>(&value)) {
					value = *boolean ? 1 : 0;
				}
			}
			bool valid = true;
			switch (member.mType) {
			case Type::VEC4:
				valid = std::holds_alternative<MATH::Vector4f>(value);
				break;
			case Type::VEC3:
				valid = std::holds_alternative<MATH::Vector3f>(value);
				break;
			case Type::VEC2:
				valid = std::holds_alternative<MATH::Vector2f>(value);
				break;
			case Type::BOOL:
				valid = std::holds_alternative<bool>(value);
				break;
			case Type::FLOAT:
				valid = std::holds_alternative<float>(value) || std::holds_alternative<int>(value);
				break;
			case Type::INT:
				valid = std::holds_alternative<int>(value);
				break;
			default:
				break;
			}
			if (!valid) {
				throw std::invalid_argument("Invalid PBR uniform type in " + resource.path + ": " + key);
			}
			std::visit(
				[&](const auto& input) {
					using Value = std::decay_t<decltype(input)>;
					if constexpr (std::is_same_v<Value, float>) {
						if (!std::isfinite(input)) {
							throw std::invalid_argument("Non-finite PBR uniform: " + key);
						}
					}
					else if constexpr (std::is_same_v<Value, MATH::Vector2f>) {
						if (!std::isfinite(input.x) || !std::isfinite(input.y)
							|| (member.mName == "u_TextureTiling" && (input.x <= 0 || input.y <= 0))) {
							throw std::invalid_argument("Invalid PBR UV transform: " + resource.path);
						}
					}
					else if constexpr (std::is_same_v<Value, MATH::Vector4f>) {
						if (!std::isfinite(input.x) || !std::isfinite(input.y) || !std::isfinite(input.z) || !std::isfinite(input.w)) {
							throw std::invalid_argument("Non-finite PBR color: " + resource.path);
						}
					}
				},
				value);
		}
	}
	if (!reflected(*result.shader, "pbr_LayersAlpha")) {
		throw std::runtime_error("Layered PBR shader failed to compile its surface contract: " + resource.ShaderPath);
	}
	unsigned samplerCount = 0;
	for (const auto& uniform : result.shader->getReflection().mUniforms) {
		using Type = ShaderReflection::UniformType;
		if ((uniform.mShaderMask & static_cast<size_t>(ShaderType::FRAGMENT)) != 0
			&& (uniform.mType == Type::SAMPLER_2D || uniform.mType == Type::SAMPLER_CUBE || uniform.mType == Type::SAMPLER_2D_ARRAY
				|| uniform.mType == Type::SAMPLER_3D)) {
			samplerCount += uniform.mDescriptorCount;
		}
	}
	if (samplerCount > DriverInterface::Get()->maxFragmentSampledTextures()) {
		throw std::runtime_error("Layered PBR exceeds fragment texture budget: " + resource.path);
	}

	const auto white = textures.createColor(fallbackPath, 255, 255, 255, 255, false);
	const auto flatNormal = textures.createColor("/__pbr_neutral_normal__", 128, 128, 255, 255, false);
	const auto put = [&](const std::string& name, const UniformData& value) {
		if (reflected(*result.shader, name)) {
			result.uniforms[name] = value;
		}
	};
	const auto putDefault = [&](const std::string& name, const UniformData& value) {
		if (!result.uniforms.contains(name)) {
			put(name, value);
		}
	};
	for (const auto* name : { "u_AlbedoMap", "u_NormalMap", "u_MetallicMap", "u_RoughnessMap", "u_AmbientOcclusionMap", "u_HeightMap" }) {
		putDefault(name, white);
	}
	if (!resource.Uniforms.contains("u_NormalMap")) {
		put("u_NormalMap", flatNormal);
	}
	putDefault("data_UBOu_Albedo", MATH::Vector4f { 1.0f, 1.0f, 1.0f, 1.0f });
	putDefault("data_UBOu_TextureTiling", MATH::Vector2f { 1.0f, 1.0f });
	putDefault("data_UBOu_TextureOffset", MATH::Vector2f { 0.0f, 0.0f });
	putDefault("data_UBOu_Metallic", 1.0f);
	putDefault("data_UBOu_Roughness", 1.0f);
	putDefault("data_UBOu_EnableNormalMapping", false);
	putDefault("data_UBOu_HeightScale", 0.0f);
	putDefault("data_UBOu_Transmission", 0.0f);
	// Plain JSON integers are valid numeric factors; convert before UBO upload.
	for (const auto* name : { "data_UBOu_Metallic", "data_UBOu_Roughness", "data_UBOu_HeightScale", "data_UBOu_Transmission" }) {
		if (auto found = result.uniforms.find(name); found != result.uniforms.end()) {
			if (const auto* number = std::get_if<int>(&found->second)) {
				found->second = static_cast<float>(*number);
			}
			const auto* number = std::get_if<float>(&found->second);
			if (!number || !std::isfinite(*number)) {
				throw std::invalid_argument("Invalid PBR factor: " + resource.path + ": " + name);
			}
		}
	}
	if (const auto transmission = result.uniforms.find("data_UBOu_Transmission"); transmission != result.uniforms.end()) {
		const float value = std::get<float>(transmission->second);
		if (value < 0 || value > 1) {
			throw std::invalid_argument("PBR transmission must be between zero and one: " + resource.path);
		}
	}
	put("pbr_LayersAlpha",
		MATH::Vector4f { resource.PbrLayers.AlphaCutoff, resource.PbrLayers.BaseHeightScale, resource.PbrLayers.BaseHeightBias,
		static_cast<float>(resource.PbrLayers.AlphaMode) });
	put("pbr_LayersBase", MATH::Vector4f { resource.Uniforms.contains("u_HeightMap") ? 1.0f : 0.0f, 0.0f, 1.0f, 1.0f });
	for (size_t index = 0; index < resource.PbrLayers.Layers.size(); ++index) {
		const auto& layer = resource.PbrLayers.Layers[index];
		const auto member = "pbr_LayersLayer" + std::to_string(index);
		const auto sampler = "pbr_Layer" + std::to_string(index);
		int flags = 0;
		const auto map = [&](const char* suffix, const std::string& path, int flag) {
			if (!path.empty()) {
				flags |= flag;
			}
			// Keep every supplied map alive, including maps optimized out of a coverage pass.
			const auto texture = path.empty() ? white : loadTexture(path, "Layers[" + std::to_string(index) + "]." + suffix);
			put(sampler + suffix, texture);
		};
		map("AlbedoMap", layer.AlbedoMap, 1);
		map("NormalMap", layer.NormalMap, 2);
		map("OrmMap", layer.OrmMap, 4);
		map("HeightMap", layer.HeightMap, 8);
		map("MaskMap", layer.MaskMap, 16);
		if (layer.InvertMask) {
			flags |= 32;
		}
		if (layer.NormalMode == PbrNormalMode::Detail) {
			flags |= 64;
		}
		put(member + "Color", layer.Color);
		put(member + "Factors", MATH::Vector4f { layer.Metallic, layer.Roughness, layer.AmbientOcclusion, layer.NormalStrength });
		put(member + "UV", MATH::Vector4f { layer.Tiling.x, layer.Tiling.y, layer.Offset.x, layer.Offset.y });
		put(member + "MaskUV", MATH::Vector4f { layer.MaskTiling.x, layer.MaskTiling.y, layer.MaskOffset.x, layer.MaskOffset.y });
		put(member + "Blend",
			MATH::Vector4f { layer.Enabled ? layer.Weight : 0.0f, layer.BlendMode == PbrBlendMode::Height ? layer.HeightInfluence : 0.0f,
			layer.TransitionWidth, static_cast<float>(layer.MaskChannel) });
		put(member + "Height", MATH::Vector4f { layer.HeightScale, layer.HeightBias, static_cast<float>(flags), 0.0f });
	}
	return result;
}

void MaterialInterface::applyMaterialState(const MaterialResource& resource) {
	mPath = resource.path;
	mParentPath = resource.parentPath;
	mNeedFileWatch = resource.NeedFileWatch;
	mPbrLayers = resource.PbrLayers;
	mDeformation = resource.Deformation;
	mBlendable = getShader()->supportsPbrLayers() ? resource.PbrLayers.AlphaMode == PbrAlphaMode::Blend : resource.Blendable;
	mBackfaceCulling = resource.BackfaceCulling;
	mFrontfaceCulling = resource.FrontfaceCulling;
	mDepthTest = resource.DepthTest;
	mDepthWriting = resource.DepthWriting;
	mColorWriting = resource.ColorWriting;
	mGpuInstances = resource.GpuInstances;
	mIsDeferred = resource.IsDeferred;
	mDepthFunc = resource.DepthFunc;
}

void MaterialInterface::completeDescriptor(MaterialResource& resource) const {
	resource.path = mPath;
	resource.parentPath = mParentPath;
	resource.NeedFileWatch = mNeedFileWatch;
	resource.PbrLayers = mPbrLayers;
	resource.Deformation = mDeformation;
	if (!getShader() || !getShader()->supportsPbrLayers()) {
		return;
	}
	if (auto normal = resource.Uniforms.find("data_UBOu_EnableNormalMapping"); normal != resource.Uniforms.end()) {
		if (const auto* value = std::get_if<int>(&normal->second)) {
			normal->second = *value != 0;
		}
	}
	for (auto entry = resource.Uniforms.begin(); entry != resource.Uniforms.end();) {
		const auto* texture = std::get_if<std::string>(&entry->second);
		if (entry->first.starts_with("pbr_") || (texture && (texture->empty() || texture->starts_with("/__pbr_neutral_")))) {
			entry = resource.Uniforms.erase(entry);
		}
		else {
			++entry;
		}
	}
}
} // namespace IKIGAI::RENDER
