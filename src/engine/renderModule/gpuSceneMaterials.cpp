#include "gpuSceneMaterials.h"

#if defined(IKIGAI_USE_GPU_DRIVEN)
#include "render.h"
#include <algorithm>
#include <bit>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	using Uniforms = std::map<std::string, MaterialInterface::UniformData>;
	using UniformType = ShaderReflection::UniformType;

	template <class T>
	const T& ResolveUniform(
		const MaterialInterface& material, const Uniforms& defaults, const char* key, const char* alternate = nullptr, const char* alias = nullptr) {
		const auto* value = material.findUniform(key);
		if (!value && alternate) {
			value = material.findUniform(alternate);
		}
		if (!value && alias) {
			value = material.findUniform(alias);
		}
		return std::get<T>(value ? *value : defaults.at(key));
	}

	std::shared_ptr<MaterialInterface> CreatePass(Renderer& renderer, MaterialInterface& geometryPass, const char* suffix, const char* shaderPath) {
		auto desc = geometryPass.getDescriptor();
		desc.path += suffix;
		desc.ShaderPath = shaderPath;
		desc.Uniforms.clear();
		return renderer.createMaterial(desc);
	}
}

GpuSceneMaterials::GpuSceneMaterials(Renderer& renderer, MaterialInterface& geometryPass)
	: mPass(CreatePass(renderer, geometryPass, "#gpu-material-table", "/shaders/render/portable/gbufferGpuMaterials.shader")) {
	resolveBindings();
}

void GpuSceneMaterials::loadGeometryPass(Renderer& renderer, MaterialInterface& geometryPass) {
	auto pass = CreatePass(renderer, geometryPass, "#gpu-geometry-pool", "/shaders/render/portable/gbufferGpuGeometry.shader");
	const auto shader = pass ? pass->getShader() : nullptr;
	if (!shader) {
		throw std::runtime_error("Missing GPU geometry pool shader");
	}
	const auto& reflection = shader->getReflection();
	const auto& ids = reflection.mUniforms.at(reflection.mNameToUniforms.at("engine_GpuMaterialIds"));
	const auto& rows = reflection.mUniforms.at(reflection.mNameToUniforms.at("engine_GpuMaterials"));
	const auto& textures = reflection.mUniforms.at(reflection.mNameToUniforms.at("engine_GpuMaterialTextures"));
	if (ids.mType != UniformType::STORAGE_BUFFER || ids.mBind != mIdsBinding || rows.mType != UniformType::STORAGE_BUFFER
		|| rows.mBind != mRowsBinding || textures.mType != UniformType::SAMPLER_2D || textures.mBind != mTexturesBinding
		|| !shader->isBindlessResource(textures) || shader->descriptorCount(textures) != mTextureCapacity) {
		throw std::runtime_error("GPU geometry pool shader has incompatible material bindings");
	}
	mGeometryPass = std::move(pass);
}

void GpuSceneMaterials::resolveBindings() {
	const auto shader = mPass ? mPass->getShader() : nullptr;
	if (!shader) {
		throw std::runtime_error("Missing GPU scene material shader");
	}
	const auto& reflection = shader->getReflection();
	const auto require = [&](const char* name, UniformType type) -> const ShaderReflection::Uniform& {
		const auto& uniform = reflection.mUniforms.at(reflection.mNameToUniforms.at(name));
		if (uniform.mType != type) {
			throw std::runtime_error(std::string("Unexpected GPU material binding type for ") + name);
		}
		return uniform;
	};
	mIdsBinding = require("engine_GpuMaterialIds", UniformType::STORAGE_BUFFER).mBind;
	mRowsBinding = require("engine_GpuMaterials", UniformType::STORAGE_BUFFER).mBind;
	const auto& textures = require("engine_GpuMaterialTextures", UniformType::SAMPLER_2D);
	const auto capacity = shader->descriptorCount(textures);
	if (!shader->isBindlessResource(textures) || (mTextureCapacity && (mTexturesBinding != textures.mBind || mTextureCapacity != capacity))) {
		throw std::runtime_error("Incompatible GPU scene texture table");
	}
	mTexturesBinding = static_cast<uint32_t>(textures.mBind);
	mTextureCapacity = capacity;
	mGeneration = shader->generation();
}

GpuSceneMaterialIds GpuSceneMaterials::update(
	Renderer& renderer, const GpuSceneSnapshot& snapshot, const Uniforms& defaults, const std::shared_ptr<TextureInterface>& fallback) {
	if (mGeneration != mPass->getShader()->generation()) {
		resolveBindings();
	}
	struct Candidate {
		Row Data {};
		std::shared_ptr<TextureInterface> Albedo;
		std::shared_ptr<TextureInterface> Normal;
	};
	std::map<const MaterialInterface*, Candidate> candidates;
	std::set<const MaterialInterface*> visited;
	std::set<const TextureInterface*> wanted;
	for (const auto& instance : snapshot.Instances) {
		const auto* material = instance.Material.get();
		if (!material || !visited.insert(material).second || material->isBlendable() || material->pbrLayers().AlphaMode != PbrAlphaMode::Opaque
			|| !material->pbrLayers().Layers.empty() || material->deformation().Mode != DeformationMode::None) {
			continue;
		}
		try {
			using Texture = std::shared_ptr<TextureInterface>;
			Candidate candidate;
			const auto& color = ResolveUniform<MATH::Vector4f>(*material, defaults, "data_UBOu_Albedo", "u_Albedo", "data_UBOu_Diffuse");
			const auto& tiling = ResolveUniform<MATH::Vector2f>(*material, defaults, "data_UBOu_TextureTiling", "u_TextureTiling");
			const auto& offset = ResolveUniform<MATH::Vector2f>(*material, defaults, "data_UBOu_TextureOffset", "u_TextureOffset");
			candidate.Data.Albedo = { color.x, color.y, color.z, color.w };
			candidate.Data.UvTransform = { tiling.x, tiling.y, offset.x, offset.y };
			candidate.Data.Surface[0] = ResolveUniform<float>(*material, defaults, "data_UBOu_Metallic", "u_Metallic");
			candidate.Data.Surface[1] = ResolveUniform<float>(*material, defaults, "data_UBOu_Roughness", "u_Roughness");
			candidate.Data.Textures[2] = ResolveUniform<bool>(*material, defaults, "data_UBOu_EnableNormalMapping", "u_EnableNormalMapping") ? 1 : 0;
			candidate.Albedo = ResolveUniform<Texture>(*material, defaults, "u_AlbedoMap", "u_DiffuseMap");
			if (candidate.Data.Textures[2]) {
				candidate.Normal = ResolveUniform<Texture>(*material, defaults, "u_NormalMap");
			}
			const auto ordinary = [](const Texture& texture) { return !texture || texture->getType() == TextureType::TEXTURE_2D; };
			if (!ordinary(candidate.Albedo) || !ordinary(candidate.Normal)) {
				continue;
			}
			wanted.insert(candidate.Albedo.get());
			wanted.insert(candidate.Normal.get());
			candidates.emplace(material, std::move(candidate));
		} catch (const std::bad_variant_access&) {
			// A source with a different uniform representation keeps its existing draw.
		}
	}

	if (candidates.empty() && mTextureSlots.empty()) {
		return {};
	}
	DriverInterface* driver = nullptr;
	const auto access = [&]() -> DriverInterface& {
		if (!driver) {
			driver = renderer.getDriver();
		}
		return *driver;
	};
	std::erase_if(mTextureSlots, [&](const auto& item) {
		if (wanted.contains(item.first)) {
			return false;
		}
		access().releaseBindlessResource(item.second.Handle);
		return true;
	});
	if (candidates.empty()) {
		return {};
	}
	if (!mFallbackRegistered) {
		const auto defaultSlot = access().registerBindlessTexture(mTexturesBinding, fallback, mTextureCapacity);
		if (defaultSlot.index != 0) {
			throw std::runtime_error("GPU scene texture table needs its fallback in slot zero");
		}
		mFallbackRegistered = true;
	}
	const auto indexFor = [&](const std::shared_ptr<TextureInterface>& texture) -> std::optional<uint32_t> {
		if (!texture || texture == fallback) {
			return 0;
		}
		if (const auto existing = mTextureSlots.find(texture.get()); existing != mTextureSlots.end()) {
			return existing->second.Handle.index;
		}
		// The table can also contain retired slots that are waiting on frame fences.
		try {
			const auto handle = access().registerBindlessTexture(mTexturesBinding, texture, mTextureCapacity);
			mTextureSlots.emplace(texture.get(), TextureSlot { texture, handle });
			return handle.index;
		} catch (const std::runtime_error& error) {
			if (std::string_view(error.what()) != "Bindless table capacity exhausted") {
				throw;
			}
			return {};
		}
	};

	GpuSceneMaterialIds ids;
	std::vector<Row> rows;
	rows.reserve(candidates.size());
	for (auto& [material, candidate] : candidates) {
		const auto albedo = indexFor(candidate.Albedo);
		const auto normal = indexFor(candidate.Normal);
		if (!albedo || !normal) {
			continue;
		}
		candidate.Data.Textures[0] = *albedo;
		candidate.Data.Textures[1] = *normal;
		ids.emplace(material, static_cast<uint32_t>(rows.size()));
		rows.push_back(candidate.Data);
	}
	if (!rows.empty() && rows != mRows) {
		if (!mRowsBuffer || mRowsBuffer->getSizeByte() < rows.size() * sizeof(Row)) {
			mRowsBuffer
				= renderer.createStorageBuffer(nullptr, std::bit_ceil(rows.size()), sizeof(Row), BufferUsage::STORAGE | BufferUsage::TRANSFER_DST);
		}
		renderer.updateStorageBufferRange(mRowsBuffer, rows.data(), rows.size() * sizeof(Row), 0);
		mRows = std::move(rows);
	}
	return ids;
}

void GpuSceneMaterials::clear(Renderer& renderer) {
	if (!mTextureSlots.empty()) {
		auto& driver = *renderer.getDriver();
		for (const auto& [texture, slot] : mTextureSlots) {
			driver.releaseBindlessResource(slot.Handle);
		}
		mTextureSlots.clear();
	}
}

void GpuSceneMaterials::bind(Renderer& renderer, const std::shared_ptr<StorageBufferInterface>& materialIds) const {
	renderer.setStorageBuffer(mIdsBinding, materialIds);
	renderer.setStorageBuffer(mRowsBinding, mRowsBuffer);
}
}
#endif
