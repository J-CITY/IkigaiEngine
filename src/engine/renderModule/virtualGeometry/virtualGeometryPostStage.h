#pragma once

#include <memory>
#include <vector>

#include "renderModule/gameRenderer.h"

namespace IKIGAI::RENDER {
class TextureInterface;

// VIRTUAL_GEOMETRY_POST: phase 2 of the two-phase HZB occlusion of virtual geometry (M4-V2). The stage re-tests what
// phase 1 rejected against its "Hzb" binding and draws the result into its FrameBuffer without clearing it. Phase 1
// may only reject when this stage runs in the same frame after an HZB build of the phase-1 depth.
//
// Descriptor contract (checked at load): Bindings {"Hzb": <texture>}, FrameBuffer equal to the Geometry stage's,
// no Material, per frame (no Once, UpdateInterval 1), Unsupported BYPASS or ERROR.
void ValidateVirtualGeometryPostStage(const PipelineStage::Descriptor& descriptor);
// Order contract over the resolved stage list: the Hzb texture is written (Writes or DispatchTexture) by an enabled
// per-frame stage between the "Geometry" stage and the post stage.
void ValidateVirtualGeometryPostOrder(const std::vector<PipelineStage::Descriptor>& stages);

// Active post stage of the pipeline, or null: enabled, and its physical Hzb texture has the size of its framebuffer
// (the HZB maps 1:1 onto the depth it was built from). The texture is returned through `hzb`.
const PipelineStage* FindActiveVirtualGeometryPostStage(const RenderGraphPipeline& pipeline, std::shared_ptr<TextureInterface>& hzb);
}
