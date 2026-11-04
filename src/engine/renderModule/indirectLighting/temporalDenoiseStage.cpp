#include "temporalDenoiseStage.h"

#include "renderModule/backends/interface/uniformBufferInterface.h"
#include "renderModule/light.h"
#include "renderModule/render.h"

#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace IKIGAI::RENDER {
namespace {
	// Shader name of the denoiser settings block (temporalDenoiseSettings.glsl) in BufferLinks.
	constexpr const char* DenoiseBlockName = "Denoise";

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

	HistoryEffect EffectOf(TemporalDenoiseEffect effect) {
		switch (effect) {
		case TemporalDenoiseEffect::GLOBAL_ILLUMINATION:
			return HistoryEffect::GlobalIllumination;
		case TemporalDenoiseEffect::REFLECTIONS:
			return HistoryEffect::Reflections;
		}
		return HistoryEffect::GlobalIllumination;
	}

	// FNV-1a over the light records (LightOGL is the std430 image uploaded to the GPU, compared bytewise like the RT path).
	uint64_t HashLights(const std::vector<LightOGL>& lights) {
		uint64_t hash = 14695981039346656037ull;
		const auto* bytes = reinterpret_cast<const unsigned char*>(lights.data());
		const size_t size = lights.size() * sizeof(LightOGL);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
		return hash ^ static_cast<uint64_t>(lights.size());
	}
}

void ResolveTemporalDenoisers(const RenderGraphPipeline::Descriptor& descriptor) {
	std::set<std::string> names;
	for (const auto& settings : descriptor.TemporalDenoisers) {
		ResolveTemporalDenoiseGpuSettings(settings);
		if (!names.insert(settings.Name).second) {
			throw std::runtime_error("TemporalDenoisers lists " + settings.Name + " twice");
		}
		if (settings.Name == ScreenProbeBufferName) {
			throw std::runtime_error("TemporalDenoisers name is reserved for the ScreenProbes block: " + settings.Name);
		}
		for (const auto& buffer : descriptor.Buffers) {
			if (buffer.Name == settings.Name) {
				throw std::runtime_error("TemporalDenoisers name clashes with pipeline buffer " + settings.Name);
			}
		}
	}
	std::set<std::string> linked;
	for (const auto* stage : AllStages(descriptor)) {
		const auto link = stage->BufferLinks.find(DenoiseBlockName);
		if (link == stage->BufferLinks.end()) {
			continue;
		}
		if (!names.contains(link->second)) {
			throw std::runtime_error("Stage " + stage->Name + " links Denoise to " + link->second + ", which is not a TemporalDenoisers entry");
		}
		linked.insert(link->second);
	}
	for (const auto& name : names) {
		if (!linked.contains(name)) {
			throw std::runtime_error("TemporalDenoisers entry " + name + " is not linked by any stage (BufferLinks { \"Denoise\": name })");
		}
	}
}

void CreateTemporalDenoiseBuffers(RenderGraphPipeline& pipeline, Renderer& renderer) {
	auto& runtime = pipeline.mTemporalDenoise;
	runtime = {};
	for (const auto& settings : pipeline.mDescriptor.TemporalDenoisers) {
		TemporalDenoiseRuntime::Entry entry;
		entry.Name = settings.Name;
		entry.Effect = settings.Effect;
		entry.Gpu = ResolveTemporalDenoiseGpuSettings(settings);
		pipeline.mUniformBuffers[entry.Name] = renderer.createUniformBuffer(&entry.Gpu, sizeof(entry.Gpu));
		entry.Uploaded = true;
		std::cout << "TEMPORAL_DENOISE name=" << entry.Name << " effect=" << static_cast<int>(settings.Effect)
				  << " max_history=" << settings.MaxHistory << " clamp_sigma=" << settings.ClampSigma << " clamp_stride=" << settings.ClampStride
				  << " depth_tolerance=" << settings.DepthTolerance << " normal_threshold=" << settings.NormalThreshold
				  << " anti_lag_sigma=" << settings.AntiLagSigma << std::endl;
		runtime.Entries.push_back(std::move(entry));
	}
}

void UpdateTemporalDenoiseBuffers(RenderGraphPipeline& pipeline, bool frameHistoryValid) {
	for (auto& entry : pipeline.mTemporalDenoise.Entries) {
		const int32_t valid = frameHistoryValid && pipeline.mHistory.isValid(EffectOf(entry.Effect)) ? 1 : 0;
		if (entry.Uploaded && entry.Gpu.HistoryValid == valid) {
			continue;
		}
		entry.Gpu.HistoryValid = valid;
		pipeline.mUniformBuffers.at(entry.Name)->setData(entry.Gpu);
		entry.Uploaded = true;
	}
}

void DetectTemporalDenoiseLightChange(RenderGraphPipeline& pipeline, const std::vector<LightOGL>& lights) {
	auto& runtime = pipeline.mTemporalDenoise;
	const auto hash = HashLights(lights);
	if (runtime.LightHashValid && hash != runtime.LightHash) {
		pipeline.mHistory.invalidate(HistoryReason::LightChange);
	}
	runtime.LightHash = hash;
	runtime.LightHashValid = true;
}

bool HasTemporalDenoisers(const RenderGraphPipeline& pipeline) {
	return !pipeline.mTemporalDenoise.Entries.empty();
}
}
