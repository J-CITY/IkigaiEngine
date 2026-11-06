#pragma once

#include <memory>
#include <vector>

#include "renderModule/gameRenderer.h"

namespace IKIGAI::RENDER {
class TextureInterface;

// Phase 2 of two-phase HZB occlusion. Each post stage tests the objects rejected by its first pass against
// the current HZB and appends the survivors to the existing G-buffer.
//
// Descriptor contract (checked at load): Bindings {"Hzb": <texture>}, FrameBuffer equal to the Geometry stage's,
// no Material, per frame (no Once, UpdateInterval 1), Unsupported BYPASS or ERROR.
void ValidateGeometryOcclusionPostStage(const PipelineStage::Descriptor& descriptor);
// Order contract over the resolved stage list: the Hzb texture is written (Writes or DispatchTexture) by an enabled
// per-frame stage between the "Geometry" stage and the post stage.
void ValidateGeometryOcclusionPostOrder(const std::vector<PipelineStage::Descriptor>& stages);

// Active post stage of the pipeline, or null: enabled, and its physical Hzb texture has the size of its framebuffer
// (the HZB maps 1:1 onto the depth it was built from). The texture is returned through `hzb`.
const PipelineStage* FindActiveGeometryOcclusionPostStage(
	const RenderGraphPipeline& pipeline, DrawContent kind, std::shared_ptr<TextureInterface>& hzb);
}
