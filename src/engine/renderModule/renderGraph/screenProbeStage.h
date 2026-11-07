#pragma once
#include "renderModule/gameRenderer.h"

namespace IKIGAI::RENDER {
class Renderer;

// Load-time resolution of the pipeline "ScreenProbes" block: validates the parameters, resolves the probe and atlas
// texture names and derives their Scale from TileSize / DirectionsPerProbe, and checks that the stages linking
// ScreenProbeBufferName have an enabled block. Throws std::runtime_error.
void ResolveScreenProbes(RenderGraphPipeline::Descriptor& descriptor);
// Creates the ScreenProbeBufferName uniform buffer from the resolved block (no-op when the block is disabled).
void CreateScreenProbeBuffer(RenderGraphPipeline& pipeline, Renderer& renderer);
void UpdateScrollingDdgi(RenderGraphPipeline& pipeline, Renderer& renderer, const MATH::Vector3f& camera);
bool UpdateSdfTrace(RenderGraphPipeline& pipeline, Renderer& renderer, SCENE_SYSTEM::Scene& scene, const std::string& scenePath);
void UpdateSurfaceCache(RenderGraphPipeline& pipeline, Renderer& renderer, const std::string& scenePath, bool sceneChanged);
bool UpdateSdfCacheLights(RenderGraphPipeline& pipeline, Renderer& renderer, std::vector<LightOGL> lights);
}
