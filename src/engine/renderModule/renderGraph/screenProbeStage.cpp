#include "screenProbeStage.h"

#include "renderModule/backends/interface/uniformBufferInterface.h"
#include "renderModule/render.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	constexpr auto DdgiScrollBufferName = "DdgiScrollSettings";
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
	bool linked = false;
	for (const auto* stage : AllStages(descriptor)) {
		linked |= LinksScreenProbeBuffer(*stage);
	}
	for (const auto& buffer : descriptor.Buffers) {
		if (buffer.Name == ScreenProbeBufferName || buffer.Name == DdgiScrollBufferName) {
			throw std::runtime_error("Pipeline buffer name is reserved for ScreenProbes: " + buffer.Name);
		}
	}
	if (!settings.Enabled) {
		Require(settings.DdgiProbeBudget == 0, "DdgiProbeBudget", "requires ScreenProbes.Enabled");
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
	ScaleTextures(descriptor, settings.ProbeTextures, 1.0f / static_cast<float>(gpu.TileSize), "ProbeTextures");
	ScaleTextures(descriptor, settings.AtlasTextures, static_cast<float>(gpu.DirectionSide) / static_cast<float>(gpu.TileSize), "AtlasTextures");
	for (const auto& name : settings.ProbeTextures) {
		for (const auto& atlas : settings.AtlasTextures) {
			Require(name != atlas, "AtlasTextures", "texture " + name + " is also a probe texture");
		}
	}
	SelectTemporalStages(descriptor);
}

void CreateScreenProbeBuffer(RenderGraphPipeline& pipeline, Renderer& renderer) {
	const auto& settings = pipeline.mDescriptor.ScreenProbes;
	if (!settings.Enabled) {
		return;
	}
	const auto volume = ResolveDdgiVolume(pipeline.mDescriptor);
	const auto gpu = ResolveScreenProbeGpuSettings(settings, volume);
	pipeline.mUniformBuffers[ScreenProbeBufferName] = renderer.createUniformBuffer(&gpu, sizeof(gpu));
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
