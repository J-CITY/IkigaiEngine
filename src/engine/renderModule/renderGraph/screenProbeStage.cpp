#include "screenProbeStage.h"

#include "renderModule/backends/interface/uniformBufferInterface.h"
#include "renderModule/render.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
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
}

void ResolveScreenProbes(RenderGraphPipeline::Descriptor& descriptor) {
	const auto& settings = descriptor.ScreenProbes;
	bool linked = false;
	for (const auto* stage : AllStages(descriptor)) {
		linked |= LinksScreenProbeBuffer(*stage);
	}
	for (const auto& buffer : descriptor.Buffers) {
		if (buffer.Name == ScreenProbeBufferName) {
			throw std::runtime_error(std::string("Pipeline buffer name is reserved for the ScreenProbes block: ") + ScreenProbeBufferName);
		}
	}
	if (!settings.Enabled) {
		if (linked) {
			throw std::runtime_error(std::string("Stages link ") + ScreenProbeBufferName + " but the pipeline ScreenProbes block is not enabled");
		}
		return;
	}
	if (!linked) {
		throw std::runtime_error(std::string("ScreenProbes block is enabled but no stage links ") + ScreenProbeBufferName);
	}
	const auto gpu = ResolveScreenProbeGpuSettings(settings, ResolveDdgiVolume(descriptor));
	ScaleTextures(descriptor, settings.ProbeTextures, 1.0f / static_cast<float>(gpu.TileSize), "ProbeTextures");
	ScaleTextures(descriptor, settings.AtlasTextures, static_cast<float>(gpu.DirectionSide) / static_cast<float>(gpu.TileSize), "AtlasTextures");
	for (const auto& name : settings.ProbeTextures) {
		for (const auto& atlas : settings.AtlasTextures) {
			Require(name != atlas, "AtlasTextures", "texture " + name + " is also a probe texture");
		}
	}
}

void CreateScreenProbeBuffer(RenderGraphPipeline& pipeline, Renderer& renderer) {
	const auto& settings = pipeline.mDescriptor.ScreenProbes;
	if (!settings.Enabled) {
		return;
	}
	const auto gpu = ResolveScreenProbeGpuSettings(settings, ResolveDdgiVolume(pipeline.mDescriptor));
	pipeline.mUniformBuffers[ScreenProbeBufferName] = renderer.createUniformBuffer(&gpu, sizeof(gpu));
	std::cout << "SCREEN_PROBES tile=" << gpu.TileSize << " directions=" << gpu.DirectionSide * gpu.DirectionSide << " max_steps=" << gpu.MaxSteps
			  << " thickness=" << gpu.Thickness << " intensity=" << gpu.Intensity << " history=" << gpu.UseHistory << " debug=" << gpu.DebugMode
			  << " spatial_radius=" << gpu.SpatialRadius << " chain_mask=" << gpu.ChainMask << " previous_scene_weight=" << gpu.PreviousSceneWeight
			  << " ray_query_max_distance=" << gpu.RayQueryMaxDistance << " ddgi=" << gpu.DdgiValid << std::endl;
}
}
