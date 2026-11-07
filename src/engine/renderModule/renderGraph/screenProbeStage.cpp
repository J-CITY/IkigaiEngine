#include "screenProbeStage.h"

#include "renderModule/backends/interface/uniformBufferInterface.h"
#include "renderModule/render.h"
#include "utilsModule/jsonLoader.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	constexpr auto DdgiScrollBufferName = "DdgiScrollSettings";
	constexpr auto SurfaceCacheBufferName = "SurfaceCacheSettings";
	constexpr auto SurfaceCacheTextureName = "SPSurfaceRadiance";
	constexpr auto SdfCacheLightsBufferName = "SdfCacheLights";
	constexpr size_t SdfCacheMaxLights = 8;
	constexpr int SdfTraceActiveBit = 1 << 4;
	constexpr auto SdfAtlasTextureName = "SPSdfAtlas";
	void Require(bool condition, const std::string& field, const std::string& rule) {
		if (!condition) {
			throw std::runtime_error("ScreenProbes." + field + " " + rule);
		}
	}

	bool LinksScreenProbeBuffer(const PipelineStage::Descriptor& stage) {
		for (const auto& [shaderName, bufferName] : stage.BufferLinks) {
			if (bufferName == ScreenProbeBufferName) {
				return true;
			}
		}
		return false;
	}

	std::vector<const PipelineStage::Descriptor*> AllStages(const RenderGraphPipeline::Descriptor& descriptor) {
		std::vector<const PipelineStage::Descriptor*> result;
		auto append = [&](const std::vector<PipelineStage::Descriptor>& stages) {
			for (const auto& stage : stages) {
				result.push_back(&stage);
			}
		};
		append(descriptor.StartStages);
		append(descriptor.Stages);
		for (const auto& pass : descriptor.LightPasses) {
			append(pass.InitStages);
			append(pass.CaptureStages);
			append(pass.PrepareStages);
			append(pass.ResolveStages);
		}
		return result;
	}

	void ScaleTextures(RenderGraphPipeline::Descriptor& descriptor, const std::vector<std::string>& names, float scale, const std::string& field) {
		Require(!names.empty(), field, "must name at least one texture");
		std::set<std::string> seen;
		for (const auto& name : names) {
			Require(seen.insert(name).second, field, "lists " + name + " twice");
			auto texture = std::find_if(descriptor.Textures.begin(), descriptor.Textures.end(), [&](const auto& t) { return t.Name == name; });
			Require(texture != descriptor.Textures.end(), field, "references unknown texture " + name);
			Require(texture->Path.empty() && texture->Size != RenderGraphPipeline::TextureDesc::SizeBase::FIXED && !texture->Resource.width
					&& !texture->Resource.height,
				field, "texture " + name + " must be a render-sized texture (Width/Height 0, no Path, not FIXED)");
			Require(texture->Resource.storage, field, "texture " + name + " must be a storage texture");
			texture->Scale = scale;
		}
	}

	// The DDGI grid has one source of truth: the push constants of the RAY_TRACE stage named by DdgiStage.
	ScreenProbeDdgiVolume ResolveDdgiVolume(const RenderGraphPipeline::Descriptor& descriptor) {
		const auto& settings = descriptor.ScreenProbes;
		if (!settings.UseDdgi) {
			return {};
		}
		Require(!settings.DdgiStage.empty(), "DdgiStage", "must name the DDGI probe trace stage when UseDdgi is true");
		const PipelineStage::Descriptor* found = nullptr;
		for (const auto* stage : AllStages(descriptor)) {
			if (stage->Name == settings.DdgiStage) {
				found = stage;
			}
		}
		Require(found != nullptr, "DdgiStage", "references unknown stage " + settings.DdgiStage);
		Require(found->Draw == DrawContent::RAY_TRACE && !found->Program.empty(), "DdgiStage",
			"stage " + settings.DdgiStage + " must be a RAY_TRACE stage with a Program");
		return DecodeScreenProbeDdgiVolume(found->PushConstants, settings.DdgiStage);
	}

	TextureResource LoadSdfAtlas(const std::string& path) {
		auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(path);
		Require(file && file->isValid() && file->getSize() <= 8 * 1024 * 1024, "SdfAtlas", "needs a readable cooked texture of at most 8 MiB");
		auto parsed = UTILS::FromJsonStr<TextureResource>(file->readStr());
		Require(!parsed.isErr(), "SdfAtlas", "could not decode " + path);
		auto atlas = parsed.unwrap();
		Require(atlas.width >= 16 * 16 && atlas.width <= 64 * 64 && atlas.height >= 17 && atlas.height <= 1 + 8 * 65, "SdfAtlas",
			"needs a grid resolution in [16, 64] and up to eight regions");
		const int resolution = static_cast<int>(std::sqrt(atlas.width));
		Require(atlas.width == resolution * resolution && atlas.channels == 4 && atlas.pixelType == PixelFormat::RGBA_INT
				&& atlas.texType == TextureType::TEXTURE_2D && atlas.pathTexture.empty() && !atlas.isFloat && !atlas.storage && !atlas.useMipmap
				&& atlas.mipMapCount <= 1 && atlas.colorData.size() == size_t(atlas.width) * atlas.height * 4
				&& atlas.colorData.size() <= 2 * 1024 * 1024,
			"SdfAtlas", "needs a packed RGBA8 distance field with no mipmaps and at most 2 MiB of voxel data");
		const auto readFloat = [&](size_t texel) {
			const auto* bytes = atlas.colorData.data() + texel * 4;
			const uint32_t bits = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
			return std::bit_cast<float>(bits);
		};
		const bool multi = atlas.height != resolution + 1;
		const auto* header = atlas.colorData.data();
		const int regions = multi ? header[3] : 1;
		Require(!multi
				|| (header[0] == 'S' && header[1] == 'D' && header[2] == 'F' && regions >= 1 && regions <= 8
					&& atlas.height == 1 + regions * (resolution + 1)),
			"SdfAtlas", "has an invalid region header");
		struct SdfBounds {
			std::array<float, 3> Min {};
			std::array<float, 3> Max {};
		};
		std::array<SdfBounds, 8> regionBounds {};
		for (int region = 0; region < regions; ++region) {
			const size_t first = size_t(multi ? 1 + region * (resolution + 1) : 0) * atlas.width;
			const size_t metadata = first + size_t(atlas.width) * resolution;
			const float spacing = readFloat(metadata + 3);
			Require(std::isfinite(spacing) && spacing >= 0.0001f && spacing <= 128.f, "SdfAtlas", "needs a finite voxel spacing in [0.0001, 128]");
			for (size_t axis = 0; axis < 3; ++axis) {
				const float origin = readFloat(metadata + axis);
				const float end = origin + spacing * resolution;
				Require(
					std::isfinite(origin) && origin + spacing > origin && std::isfinite(end), "SdfAtlas", "has unrepresentable world-space bounds");
				regionBounds[region].Min[axis] = origin;
				regionBounds[region].Max[axis] = end;
			}
			for (size_t texel = 4; texel < 4 + 32 * 6; ++texel) {
				const float channel = readFloat(metadata + texel);
				const float maxChannel = (texel - 4) % 6 < 3 ? 1.f : 65504.f;
				Require(std::isfinite(channel) && channel >= 0.f && channel <= maxChannel, "SdfAtlas", "has invalid material data");
			}
			for (size_t byte = first * 4; byte < metadata * 4; byte += 4) {
				if (atlas.colorData[byte + 2] >= 32 || atlas.colorData[byte + 3] != 255) {
					throw std::runtime_error("ScreenProbes.SdfAtlas has invalid voxel data");
				}
			}
		}
		if (!multi) {
			return atlas;
		}
		const int nodes = 2 * regions - 1;
		std::array<SdfBounds, 15> nodeBounds {};
		std::array<bool, 15> attached {};
		std::array<bool, 8> used {};
		attached[0] = true;
		const auto contains = [](const SdfBounds& outer, const SdfBounds& inner) {
			for (size_t axis = 0; axis < 3; ++axis) {
				if (outer.Min[axis] > inner.Min[axis] || outer.Max[axis] < inner.Max[axis]) {
					return false;
				}
			}
			return true;
		};
		for (int i = nodes - 1; i >= 0; --i) {
			const size_t first = 1 + 7 * i;
			auto& node = nodeBounds[i];
			for (size_t axis = 0; axis < 3; ++axis) {
				node.Min[axis] = readFloat(first + axis);
				node.Max[axis] = readFloat(first + 3 + axis);
				Require(std::isfinite(node.Min[axis]) && std::isfinite(node.Max[axis]) && node.Min[axis] < node.Max[axis], "SdfAtlas",
					"has invalid BVH bounds");
			}
			const auto* links = header + (first + 6) * 4;
			const int left = links[0];
			const int right = links[1];
			const int region = links[2];
			Require(links[3] == 0, "SdfAtlas", "has an invalid BVH node");
			if (region != 255) {
				Require(region < regions && !used[region] && left == 255 && right == 255 && contains(node, regionBounds[region]), "SdfAtlas",
					"has an invalid BVH leaf");
				used[region] = true;
			}
			else {
				Require(left > i && left < nodes && right > i && right < nodes && left != right && !attached[left] && !attached[right]
						&& contains(node, nodeBounds[left]) && contains(node, nodeBounds[right]),
					"SdfAtlas", "has invalid BVH children");
				attached[left] = true;
				attached[right] = true;
			}
		}
		Require(std::all_of(attached.begin(), attached.begin() + nodes, [](bool value) { return value; })
				&& std::all_of(used.begin(), used.begin() + regions, [](bool value) { return value; }),
			"SdfAtlas", "has unreachable BVH nodes or regions");
		return atlas;
	}

	void AddSdfAtlas(RenderGraphPipeline::Descriptor& descriptor) {
		const auto& settings = descriptor.ScreenProbes;
		Require(!descriptor.Graph.Enabled && !settings.UseRayQuery && !settings.UseDdgi, "SdfAtlas",
			"requires the linear software pipeline with UseRayQuery and UseDdgi disabled");
		Require(!settings.SdfScene.empty(), "SdfScene", "needs the source scene path used to bake SdfAtlas");
		Require(std::ranges::none_of(descriptor.Textures, [](const auto& texture) { return texture.Name == SdfAtlasTextureName; }), "SdfAtlas",
			"reserves pipeline texture SPSdfAtlas");
		auto& texture = descriptor.Textures.emplace_back();
		texture.Name = SdfAtlasTextureName;
		texture.Resource = LoadSdfAtlas(settings.SdfAtlas);
		texture.Size = RenderGraphPipeline::TextureDesc::SizeBase::FIXED;
		texture.Persistent = true;
	}

	void ConfigureSdfTrace(RenderGraphPipeline::Descriptor& descriptor) {
		auto trace = std::ranges::find(descriptor.Stages, "SPTrace", &PipelineStage::Descriptor::Name);
		Require(trace != descriptor.Stages.end() && trace->Enabled && trace->Draw == DrawContent::COMPUTE
				&& trace->Material == "/shaders/indirectLighting/screenProbes/spTrace.mat" && !trace->Uniforms.contains("SdfAtlas"),
			"SdfAtlas", "requires the standard enabled SPTrace pass");
		trace->Material = "/shaders/indirectLighting/screenProbes/spSdfTrace.mat";
		trace->Uniforms.emplace("SdfAtlas", std::string(SdfAtlasTextureName));
	}

	void ConfigureSdfReflections(RenderGraphPipeline::Descriptor& descriptor) {
		auto& stages = descriptor.Stages;
		auto fallback = std::ranges::find(stages, "SSRToRTContract", &PipelineStage::Descriptor::Name);
		auto hardware = std::ranges::find(stages, "RTReflectionRawTrace", &PipelineStage::Descriptor::Name);
		Require(fallback != stages.end() && hardware != stages.end() && fallback < hardware && fallback->Enabled
				&& fallback->Draw == DrawContent::QUAD && fallback->Material == "/materials/render/portable/rayReflectionFallback.mat"
				&& hardware->Draw == DrawContent::RAY_TRACE && hardware->Program == "/shaders/raytracing/glossyReflection.shader"
				&& hardware->Bypass.contains("RTReflectionRaw") && hardware->Bypass.at("RTReflectionRaw") == "RTReflectionFallback",
			"SdfReflections", "requires the standard hybrid-reflections passes in order");
		fallback->Material = "/materials/render/portable/glossySdfReflection.mat";
		for (const auto* binding : { "Depth", "Normals", "Albedo", "Roughness", "EnvironmentSpecular", "Environment" }) {
			Require(std::ranges::any_of(descriptor.Textures, [&](const auto& texture) { return texture.Name == binding; }), "SdfReflections",
				"needs texture " + std::string(binding));
			fallback->Uniforms.insert_or_assign(binding, std::string(binding));
		}
		fallback->Uniforms.insert_or_assign("SdfAtlas", std::string(SdfAtlasTextureName));
		fallback->BufferLinks.insert_or_assign("ScreenProbe", ScreenProbeBufferName);
		hardware->Enabled = false;
	}

	struct SurfaceCardAsset {
		MATH::Vector3f Origin {};
		MATH::Vector3f AxisU {};
		MATH::Vector3f AxisV {};
		MATH::Vector3f Albedo {};
		MATH::Vector3f Emissive {};
		float Roughness = 1.f;
		float Thickness = 0.005f;

		template <class Context> static void serde(Context& context, SurfaceCardAsset& value) {
			using Self = SurfaceCardAsset;
			serde::serde_struct(context, value)
				.field(&Self::Origin, "Origin")
				.field(&Self::AxisU, "AxisU")
				.field(&Self::AxisV, "AxisV")
				.field(&Self::Albedo, "Albedo")
				.field(&Self::Emissive, "Emissive")
				.field(&Self::Roughness, "Roughness")
				.field(&Self::Thickness, "Thickness");
		}
	};

	struct SurfaceCacheAsset {
		std::string Scene;
		std::vector<SurfaceCardAsset> Cards;

		template <class Context> static void serde(Context& context, SurfaceCacheAsset& value) {
			serde::serde_struct(context, value).field(&SurfaceCacheAsset::Scene, "Scene").field(&SurfaceCacheAsset::Cards, "Cards");
		}
	};

	SurfaceCacheConfig LoadSurfaceCache(const std::string& path) {
		auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(path);
		Require(file && file->isValid() && file->getSize() <= 64 * 1024, "SurfaceCache", "needs a readable card asset of at most 64 KiB");
		auto parsed = UTILS::FromJsonStr<SurfaceCacheAsset>(file->readStr());
		Require(!parsed.isErr(), "SurfaceCache", "could not decode " + path);
		const auto asset = parsed.unwrap();
		Require(!asset.Scene.empty() && !asset.Cards.empty() && asset.Cards.size() <= SurfaceCacheMaxCards, "SurfaceCache",
			"needs a scene path and 1 to 16 cards");
		SurfaceCacheConfig result;
		result.Scene = RESOURCES::FileSystem::normalizePath(asset.Scene);
		result.Gpu.State[0] = static_cast<int32_t>(asset.Cards.size());
		const auto bounded = [](float x, float low, float high) { return std::isfinite(x) && x >= low && x <= high; };
		const auto vectorBounded = [&](const MATH::Vector3f& v, float low, float high) {
			return bounded(v.x, low, high) && bounded(v.y, low, high) && bounded(v.z, low, high);
		};
		for (size_t i = 0; i < asset.Cards.size(); ++i) {
			const auto& source = asset.Cards[i];
			const auto& u = source.AxisU;
			const auto& v = source.AxisV;
			const auto lengthU = std::hypot(u.x, u.y, u.z);
			const auto lengthV = std::hypot(v.x, v.y, v.z);
			Require(vectorBounded(source.Origin, -10000.f, 10000.f) && bounded(lengthU, 0.01f, 128.f) && bounded(lengthV, 0.01f, 128.f)
					&& std::abs(u.x * v.x + u.y * v.y + u.z * v.z) <= 1e-4f * lengthU * lengthV && bounded(source.Thickness, 0.0001f, 0.02f)
					&& vectorBounded(source.Albedo, 0.f, 1.f) && vectorBounded(source.Emissive, 0.f, 65504.f) && bounded(source.Roughness, 0.7f, 1.f),
				"SurfaceCache", "has invalid bounds or material at card " + std::to_string(i));
			auto& card = result.Gpu.Cards[i];
			card.OriginTolerance = { source.Origin.x, source.Origin.y, source.Origin.z, source.Thickness };
			card.AxisU = { u.x, u.y, u.z, 0.f };
			card.AxisV = { v.x, v.y, v.z, 0.f };
			card.AlbedoRoughness = { source.Albedo.x, source.Albedo.y, source.Albedo.z, source.Roughness };
			card.Emissive = { source.Emissive.x, source.Emissive.y, source.Emissive.z, 0.f };
		}
		return result;
	}

	void ConfigureSdfSurfaceCache(RenderGraphPipeline::Descriptor& descriptor, PipelineStage::Descriptor relight) {
		const auto& settings = descriptor.ScreenProbes;
		Require(
			settings.Enabled && !settings.UseRayQuery && descriptor.SurfaceCache->Scene == RESOURCES::FileSystem::normalizePath(settings.SdfScene),
			"SurfaceCache", "requires enabled screen probes and cards baked for the SDF scene");
		auto& stages = descriptor.Stages;
		auto trace = std::ranges::find(stages, "SPTrace", &PipelineStage::Descriptor::Name);
		Require(trace != stages.end() && trace->Enabled && trace->Material == "/shaders/indirectLighting/screenProbes/spSdfTrace.mat", "SurfaceCache",
			"requires the standard SDF screen trace");
		auto& lights = descriptor.Buffers.emplace_back();
		lights.Name = SdfCacheLightsBufferName;
		lights.Type = RenderGraphPipeline::BufferType::STORAGE;
		lights.Count = 1;
		lights.Stride = sizeof(LightOGL);
		relight.Material = "/shaders/indirectLighting/screenProbes/spSdfSurfaceRelight.mat";
		relight.DispatchTexture = SurfaceCacheTextureName;
		relight.Compute.Requires = trace->Compute.Requires;
		relight.Reads = { SdfAtlasTextureName };
		relight.Bypass = { { SurfaceCacheTextureName, trace->Bypass.at("SPRadiance") } };
		relight.Uniforms.emplace("SurfaceRadiance", std::string(SurfaceCacheTextureName));
		relight.Uniforms.emplace("SdfAtlas", std::string(SdfAtlasTextureName));
		relight.BufferLinks = { { "ScreenProbe", ScreenProbeBufferName }, { "SurfaceCache", SurfaceCacheBufferName },
			{ "SdfCacheLights", SdfCacheLightsBufferName } };
		trace->Material = "/shaders/indirectLighting/screenProbes/spSdfCachedTrace.mat";
		trace->Uniforms.emplace("SurfaceRadiance", std::string(SurfaceCacheTextureName));
		trace->BufferLinks.insert(relight.BufferLinks.begin(), relight.BufferLinks.end());
		trace->Reads.push_back(SurfaceCacheTextureName);
		stages.insert(trace, std::move(relight));
	}

	void ConfigureSurfaceCache(RenderGraphPipeline::Descriptor& descriptor) {
		const auto& settings = descriptor.ScreenProbes;
		const bool software = !settings.SdfAtlas.empty();
		Require(!descriptor.Graph.Enabled && (software || settings.UseRayQuery), "SurfaceCache", "requires the linear SDF or ray-query pipeline");
		auto& stages = descriptor.Stages;
		Require(std::ranges::none_of(stages, [](const auto& stage) { return stage.Name == SurfaceCacheRelightStageName; })
				&& std::ranges::none_of(descriptor.Textures, [](const auto& texture) { return texture.Name == SurfaceCacheTextureName; }),
			"SurfaceCache", "reserves SPSurfaceRelight and SPSurfaceRadiance");
		descriptor.SurfaceCache = LoadSurfaceCache(settings.SurfaceCache);
		auto& texture = descriptor.Textures.emplace_back();
		texture.Name = SurfaceCacheTextureName;
		texture.Size = RenderGraphPipeline::TextureDesc::SizeBase::FIXED;
		texture.Persistent = true;
		texture.Resource.width = SurfaceCacheCardSide;
		texture.Resource.height = SurfaceCacheCardSide * descriptor.SurfaceCache->Gpu.State[0];
		texture.Resource.channels = 4;
		texture.Resource.pixelType = PixelFormat::RGBA16_FLOAT;
		texture.Resource.isFloat = true;
		texture.Resource.storage = true;
		texture.Resource.useMipmap = false;

		PipelineStage::Descriptor relight;
		relight.Name = SurfaceCacheRelightStageName;
		relight.Draw = DrawContent::COMPUTE;
		relight.Unsupported = "BYPASS";
		relight.Once = true;
		relight.Writes = { texture.Name };
		if (software) {
			ConfigureSdfSurfaceCache(descriptor, std::move(relight));
			return;
		}
		auto trace = std::ranges::find(stages, "SPRayQueryTrace", &PipelineStage::Descriptor::Name);
		Require(trace != stages.end() && trace->Enabled && trace->Draw == DrawContent::COMPUTE
				&& trace->Program == "/shaders/indirectLighting/screenProbes/spRayQueryTrace.shader" && !trace->Bindings.contains("SurfaceCache")
				&& !trace->Bindings.contains("SurfaceRadiance"),
			"SurfaceCache", "requires the standard SPRayQueryTrace pass");
		relight.Program = "/shaders/indirectLighting/screenProbes/spSurfaceRelight.shader";
		relight.HitGroupOffset = trace->HitGroupOffset;
		relight.LaunchTexture = texture.Name;
		relight.Reads = { "@Scene" };
		relight.Bypass = { { texture.Name, trace->Bypass.at("SPRadianceRT") } };
		for (const auto* binding : { "SceneAS", "RTGeometry", "RTInstances", "RTAlbedoMaps", "engine_Lights" }) {
			relight.Bindings.emplace(binding, trace->Bindings.at(binding));
		}
		relight.Bindings.emplace("SurfaceCache", SurfaceCacheBufferName);
		relight.Bindings.emplace("SurfaceRadiance", texture.Name);
		trace->Program = "/shaders/indirectLighting/screenProbes/spRayQuerySurfaceCache.shader";
		trace->Bindings.emplace("SurfaceCache", SurfaceCacheBufferName);
		trace->Bindings.emplace("SurfaceRadiance", texture.Name);
		trace->Reads.push_back(texture.Name);
		stages.insert(trace, std::move(relight));
	}

	void ConfigureScrollingDdgi(RenderGraphPipeline::Descriptor& descriptor, const ScreenProbeDdgiVolume& volume) {
		const auto& settings = descriptor.ScreenProbes;
		const int budget = settings.DdgiProbeBudget;
		const int count = volume.CountX * volume.CountY * volume.CountZ;
		Require(budget > 0 && volume.Valid && count <= MaxScrollingDdgiProbes && budget <= count, "DdgiProbeBudget",
			"requires a DDGI grid of at most 256 probes and a budget in [1, grid size]");
		Require(!descriptor.Graph.Enabled && volume.Spacing <= 1024.f, "DdgiProbeBudget",
			"requires the linear pipeline and probe spacing of at most 1024 world units");
		auto& stages = descriptor.Stages;
		auto trace = std::ranges::find(stages, settings.DdgiStage, &PipelineStage::Descriptor::Name);
		auto update = std::ranges::find(stages, "DDGIProbeUpdate", &PipelineStage::Descriptor::Name);
		Require(trace != stages.end() && update != stages.end() && trace < update && trace->Enabled && update->Enabled
				&& trace->Program == "/shaders/raytracing/ddgi.shader" && update->Draw == DrawContent::COMPUTE
				&& update->Material == "/materials/render/portable/ddgiUpdate.mat",
			"DdgiProbeBudget", "requires the standard DDGI trace and update passes in that order");
		Require(trace->LaunchTexture.empty() && trace->LaunchX == volume.Rays && trace->LaunchY == count && trace->LaunchZ == 1, "DdgiProbeBudget",
			"requires the authored dispatch to match the DDGI grid");
		const auto updateTexture = [&](const char* binding) {
			const auto uniform = update->Uniforms.find(binding);
			const auto* name = uniform == update->Uniforms.end() ? nullptr : std::get_if<std::string>(&uniform->second);
			return name ? *name : std::string {};
		};
		for (const auto* binding : { "Irradiance", "Distance" }) {
			const auto name = updateTexture(binding);
			const auto texture = std::ranges::find(descriptor.Textures, name, &RenderGraphPipeline::TextureDesc::Name);
			const auto previousBinding = std::string("Previous") + binding;
			const auto previous = trace->Bindings.find(previousBinding);
			Require(texture != descriptor.Textures.end() && texture->Path.empty() && texture->Resource.storage && texture->History
					&& texture->Persistent && texture->Resource.width == 10 * volume.CountX
					&& texture->Resource.height == 10 * volume.CountY * volume.CountZ && updateTexture(previousBinding.c_str()) == name + "Previous"
					&& previous != trace->Bindings.end() && previous->second == name + "Previous",
				"DdgiProbeBudget", "requires matching persistent current and history probe atlases");
		}
		Require(update->DispatchTexture == updateTexture("Irradiance"), "DdgiProbeBudget", "requires dispatch over the irradiance atlas");
		for (const auto* binding : { "RayRadiance", "RayDirectionDistance" }) {
			const auto name = trace->Bindings.find(binding);
			auto texture = name == trace->Bindings.end()
				? descriptor.Textures.end()
				: std::ranges::find(descriptor.Textures, name->second, &RenderGraphPipeline::TextureDesc::Name);
			Require(texture != descriptor.Textures.end() && updateTexture(binding) == name->second && texture->Path.empty()
					&& texture->Resource.storage && !texture->History && texture->Resource.width == volume.Rays && texture->Resource.height == count,
				"DdgiProbeBudget", "requires fixed ray scratch textures matching the authored dispatch");
			texture->Resource.height = budget;
		}
		trace->LaunchY = budget;
		trace->Program = "/shaders/raytracing/ddgiScroll.shader";
		trace->Bindings.emplace("DdgiScroll", DdgiScrollBufferName);
		update->Material = "/materials/render/portable/ddgiScrollUpdate.mat";
		update->BufferLinks.emplace("DdgiScroll", DdgiScrollBufferName);
	}

	// Enables one temporal stack and disables the other. A disabled stage applies its Bypass aliases every frame, so the
	// graph is rewired without editing stage inputs (M4 probe history -> raw probe irradiance; M3 SPIndirect -> denoiser output).
	void SelectTemporalStages(RenderGraphPipeline::Descriptor& descriptor) {
		const auto& settings = descriptor.ScreenProbes;
		const std::set<std::string> v2(settings.TemporalV2Stages.begin(), settings.TemporalV2Stages.end());
		const std::set<std::string> legacy(settings.LegacyTemporalStages.begin(), settings.LegacyTemporalStages.end());
		Require(v2.size() == settings.TemporalV2Stages.size(), "TemporalV2Stages", "lists a stage twice");
		Require(legacy.size() == settings.LegacyTemporalStages.size(), "LegacyTemporalStages", "lists a stage twice");
		for (const auto& name : v2) {
			Require(!legacy.contains(name), "TemporalV2Stages", "stage " + name + " is also listed in LegacyTemporalStages");
		}
		Require(!settings.UseTemporalV2 || !v2.empty(), "TemporalV2Stages", "must name the M4 temporal stages when UseTemporalV2 is true");
		const bool active = ScreenProbeTemporalV2Active(settings);
		size_t found = 0;
		for (auto& stage : descriptor.Stages) {
			const bool isV2 = v2.contains(stage.Name);
			const bool isLegacy = legacy.contains(stage.Name);
			if (!isV2 && !isLegacy) {
				continue;
			}
			++found;
			const std::string field = isV2 ? "TemporalV2Stages" : "LegacyTemporalStages";
			Require(stage.Draw == DrawContent::COMPUTE, field, "stage " + stage.Name + " must be a COMPUTE stage");
			for (const auto& output : stage.Writes) {
				Require(stage.Bypass.contains(output), field, "stage " + stage.Name + " needs a Bypass alias for " + output);
			}
			stage.Enabled = stage.Enabled && (isV2 ? active : !active);
		}
		Require(found == v2.size() + legacy.size(), "TemporalV2Stages", "names a stage that is not in Stages");
	}
}

void ResolveScreenProbes(RenderGraphPipeline::Descriptor& descriptor) {
	const auto& settings = descriptor.ScreenProbes;
	Require(settings.SdfScene.empty() || !settings.SdfAtlas.empty(), "SdfScene", "requires SdfAtlas");
	Require(!settings.SdfReflections || !settings.SdfAtlas.empty(), "SdfReflections", "requires SdfAtlas");
	bool linked = false;
	for (const auto* stage : AllStages(descriptor)) {
		linked |= LinksScreenProbeBuffer(*stage);
	}
	for (const auto& buffer : descriptor.Buffers) {
		if (buffer.Name == ScreenProbeBufferName || buffer.Name == DdgiScrollBufferName || buffer.Name == SurfaceCacheBufferName
			|| buffer.Name == SdfCacheLightsBufferName) {
			throw std::runtime_error("Pipeline buffer name is reserved for ScreenProbes: " + buffer.Name);
		}
	}
	if (!settings.SdfAtlas.empty()) {
		AddSdfAtlas(descriptor);
	}
	if (settings.SdfReflections) {
		ConfigureSdfReflections(descriptor);
	}
	if (!settings.Enabled) {
		Require(settings.DdgiProbeBudget == 0, "DdgiProbeBudget", "requires ScreenProbes.Enabled");
		Require(settings.SdfAtlas.empty() || settings.SdfReflections, "SdfAtlas", "requires ScreenProbes.Enabled or SdfReflections");
		Require(settings.SurfaceCache.empty(), "SurfaceCache", "requires ScreenProbes.Enabled");
		if (linked) {
			throw std::runtime_error(std::string("Stages link ") + ScreenProbeBufferName + " but the pipeline ScreenProbes block is not enabled");
		}
		return;
	}
	if (!linked) {
		throw std::runtime_error(std::string("ScreenProbes block is enabled but no stage links ") + ScreenProbeBufferName);
	}
	const auto volume = ResolveDdgiVolume(descriptor);
	const auto gpu = ResolveScreenProbeGpuSettings(settings, volume);
	if (settings.DdgiProbeBudget != 0) {
		ConfigureScrollingDdgi(descriptor, volume);
	}
	if (!settings.SdfAtlas.empty()) {
		ConfigureSdfTrace(descriptor);
	}
	if (!settings.SurfaceCache.empty()) {
		ConfigureSurfaceCache(descriptor);
	}
	ScaleTextures(descriptor, settings.ProbeTextures, 1.0f / static_cast<float>(gpu.TileSize), "ProbeTextures");
	ScaleTextures(descriptor, settings.AtlasTextures, static_cast<float>(gpu.DirectionSide) / static_cast<float>(gpu.TileSize), "AtlasTextures");
	for (const auto& name : settings.ProbeTextures) {
		for (const auto& atlas : settings.AtlasTextures) {
			Require(name != atlas, "AtlasTextures", "texture " + name + " is also a probe texture");
		}
	}
	SelectTemporalStages(descriptor);
}

void UpdateSdfTrace(RenderGraphPipeline& pipeline, Renderer& renderer, const std::string& scenePath) {
	const auto& settings = pipeline.mDescriptor.ScreenProbes;
	if (settings.SdfAtlas.empty()) {
		return;
	}
	auto gpu = ResolveScreenProbeGpuSettings(settings, {});
	if (!scenePath.empty() && RESOURCES::FileSystem::normalizePath(scenePath) == RESOURCES::FileSystem::normalizePath(settings.SdfScene)) {
		gpu.ChainMask |= SdfTraceActiveBit;
	}
	renderer.updateUniformBuffer(pipeline.mUniformBuffers.at(ScreenProbeBufferName), gpu);
}

void CreateScreenProbeBuffer(RenderGraphPipeline& pipeline, Renderer& renderer) {
	const auto& settings = pipeline.mDescriptor.ScreenProbes;
	if (!settings.Enabled && !settings.SdfReflections) {
		return;
	}
	const auto volume = ResolveDdgiVolume(pipeline.mDescriptor);
	const auto gpu = ResolveScreenProbeGpuSettings(settings, volume);
	pipeline.mUniformBuffers[ScreenProbeBufferName] = renderer.createUniformBuffer(&gpu, sizeof(gpu));
	if (pipeline.mDescriptor.SurfaceCache) {
		const SurfaceCacheGpu inactive;
		pipeline.mUniformBuffers[SurfaceCacheBufferName] = renderer.createUniformBuffer(&inactive, sizeof(inactive));
		pipeline.mSurfaceCacheActive = false;
	}
	if (settings.DdgiProbeBudget > 0) {
		pipeline.mDdgiScroll.Volume = volume;
		pipeline.mDdgiScroll.ScreenProbes = gpu;
		const DdgiScrollGpu initial;
		pipeline.mUniformBuffers[DdgiScrollBufferName] = renderer.createUniformBuffer(&initial, sizeof(initial));
	}
	std::cout << "SCREEN_PROBES tile=" << gpu.TileSize << " directions=" << gpu.DirectionSide * gpu.DirectionSide << " max_steps=" << gpu.MaxSteps
			  << " thickness=" << gpu.Thickness << " intensity=" << gpu.Intensity << " history=" << gpu.UseHistory << " debug=" << gpu.DebugMode
			  << " spatial_radius=" << gpu.SpatialRadius << " chain_mask=" << gpu.ChainMask << " previous_scene_weight=" << gpu.PreviousSceneWeight
			  << " ray_query_max_distance=" << gpu.RayQueryMaxDistance << " ddgi=" << gpu.DdgiValid
			  << " temporal_v2=" << (ScreenProbeTemporalV2Active(settings) ? 1 : 0) << std::endl;
}

void UpdateSurfaceCache(RenderGraphPipeline& pipeline, Renderer& renderer, const std::string& scenePath, bool sceneChanged) {
	const auto& config = pipeline.mDescriptor.SurfaceCache.value();
	const auto relight = std::ranges::find_if(pipeline.mStages, [](const auto& stage) { return stage->mName == SurfaceCacheRelightStageName; });
	const bool active = relight != pipeline.mStages.end() && ((*relight)->mRayTracingProgram || (*relight)->mCompute) && !scenePath.empty()
		&& RESOURCES::FileSystem::normalizePath(scenePath) == config.Scene;
	if (active != pipeline.mSurfaceCacheActive) {
		auto gpu = config.Gpu;
		if (!active) {
			gpu.State[0] = 0;
		}
		renderer.updateUniformBuffer(pipeline.mUniformBuffers.at(SurfaceCacheBufferName), gpu);
		pipeline.mSurfaceCacheActive = active;
		if (relight != pipeline.mStages.end()) {
			(*relight)->mExecuted = false;
		}
	}
	if (relight != pipeline.mStages.end()) {
		if (!active) {
			(*relight)->mExecuted = true;
		}
		else if (sceneChanged || pipeline.mHistory.sceneMoved()) {
			(*relight)->mExecuted = false;
		}
	}
}

bool UpdateSdfCacheLights(RenderGraphPipeline& pipeline, Renderer& renderer, std::vector<LightOGL> lights) {
	if (lights.empty()) {
		lights.push_back(LightOGL::Inactive());
	}
	if (lights.size() > SdfCacheMaxLights) {
		lights.resize(SdfCacheMaxLights);
	}
	const auto& previous = pipeline.mSdfCacheLights;
	const auto bytes = lights.size() * sizeof(LightOGL);
	if (lights.size() == previous.size() && std::memcmp(lights.data(), previous.data(), bytes) == 0) {
		return false;
	}
	pipeline.mStorageBuffers.at(SdfCacheLightsBufferName) = renderer.createStorageBuffer(lights.data(), lights.size(), sizeof(LightOGL));
	pipeline.mSdfCacheLights = std::move(lights);
	return true;
}

void UpdateScrollingDdgi(RenderGraphPipeline& pipeline, Renderer& renderer, const MATH::Vector3f& camera) {
	const auto& settings = pipeline.mDescriptor.ScreenProbes;
	if (settings.DdgiProbeBudget == 0) {
		return;
	}
	auto& state = pipeline.mDdgiScroll;
	const auto trace = std::ranges::find_if(pipeline.mStages, [&](const auto& stage) { return stage->mName == settings.DdgiStage; });
	const bool available = trace != pipeline.mStages.end() && (*trace)->mRayTracingProgram;
	DdgiScrollGpu update;
	if (available) {
		const auto& resets = pipeline.mHistory.counters().Resets[static_cast<size_t>(HistoryEffect::GlobalIllumination)];
		constexpr std::array reasons { HistoryReason::PipelineReload, HistoryReason::SceneLoading, HistoryReason::SceneTopology,
			HistoryReason::SceneMaterial, HistoryReason::LightChange, HistoryReason::EnvironmentChange, HistoryReason::ResourceUpdate,
			HistoryReason::UserReset };
		const bool reset = std::ranges::any_of(reasons, [&](HistoryReason reason) {
			const auto index = static_cast<size_t>(reason);
			return state.Resets[index] != resets[index];
		});
		state.Resets = resets;
		update = PlanDdgiScroll(state, { camera.x, camera.y, camera.z }, settings.DdgiProbeBudget, reset);
	}
	else {
		state.Initialized = false;
		state.Valid.fill(false);
		update.Delta[3] = -1;
	}
	state.ScreenProbes.DdgiValid = available ? 1 : 0;
	state.ScreenProbes.DdgiScrolling = 1;
	state.ScreenProbes.DdgiOriginX = update.OriginSpacing[0];
	state.ScreenProbes.DdgiOriginY = update.OriginSpacing[1];
	state.ScreenProbes.DdgiOriginZ = update.OriginSpacing[2];
	renderer.updateUniformBuffer(pipeline.mUniformBuffers.at(DdgiScrollBufferName), update);
	renderer.updateUniformBuffer(pipeline.mUniformBuffers.at(ScreenProbeBufferName), state.ScreenProbes);
}
}
