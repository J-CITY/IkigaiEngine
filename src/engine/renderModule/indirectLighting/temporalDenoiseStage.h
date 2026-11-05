#pragma once
#include "renderModule/gameRenderer.h"

#include <cstdint>
#include <vector>

namespace IKIGAI::RENDER {
class Renderer;
struct LightOGL;

// Load-time resolution of the pipeline "TemporalDenoisers" list: validates every entry (names unique, not clashing with
// pipeline buffers, linked by at least one stage, parameter ranges) and checks that every stage linking a "Denoise" block
// names an entry. Throws std::runtime_error.
void ResolveTemporalDenoisers(const RenderGraphPipeline::Descriptor& descriptor);
// Creates one uniform buffer per entry (named after the entry) and the per-frame runtime state.
void CreateTemporalDenoiseBuffers(RenderGraphPipeline& pipeline, Renderer& renderer);
// Per frame, before the stages run: HistoryValid = frameHistoryValid && the entry's effect history is valid
// (HistoryValidity). Uploads only on change.
void UpdateTemporalDenoiseBuffers(Renderer& renderer, RenderGraphPipeline& pipeline, bool frameHistoryValid);
// Light change detection for pipelines with a GI denoiser but no ray traced light list (hybrid-sp): raises
// HistoryReason::LightChange when the scene light data differs from the previous frame. The first frame only records.
void DetectTemporalDenoiseLightChange(RenderGraphPipeline& pipeline, const std::vector<LightOGL>& lights);
bool HasTemporalDenoisers(const RenderGraphPipeline& pipeline);
}
