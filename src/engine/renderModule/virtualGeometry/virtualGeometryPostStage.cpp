#include "virtualGeometryPostStage.h"

#include "renderModule/backends/interface/frameBufferInterface.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "virtualGeometryModule/runtime/virtualMeshAsset.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace IKIGAI::RENDER {
namespace {
	constexpr const char* HzbBinding = "Hzb";
	constexpr const char* GeometryStageName = "Geometry";

	void Require(bool condition, const std::string& stage, const std::string& message) {
		if (!condition) {
			throw std::runtime_error("Geometry occlusion post stage " + stage + ": " + message);
		}
	}
}

void ValidateGeometryOcclusionPostStage(const PipelineStage::Descriptor& descriptor) {
	Require(descriptor.Bindings.contains(HzbBinding), descriptor.Name, "missing binding Hzb");
	Require(!descriptor.FrameBuffer.empty(), descriptor.Name, "requires the G-buffer FrameBuffer");
	Require(descriptor.Material.empty() && descriptor.Program.empty(), descriptor.Name, "does not accept Material or Program");
	Require(!descriptor.Once && descriptor.UpdateInterval == 1, descriptor.Name, "must run every frame");
	Require(!descriptor.ClearColor && !descriptor.ClearDepth, descriptor.Name, "must not clear the G-buffer (ClearColor/ClearDepth false)");
	Require(descriptor.Unsupported == "BYPASS" || descriptor.Unsupported == "ERROR", descriptor.Name, "Unsupported must be BYPASS or ERROR");
	if (descriptor.Enabled && descriptor.Unsupported == "ERROR") {
		Require(VG::IsVirtualGeometryGpuPathSupported(), descriptor.Name, "GPU geometry submission is unavailable on this backend");
#if !defined(IKIGAI_USE_GPU_DRIVEN)
		Require(descriptor.Draw != DrawContent::GPU_SCENE_OCCLUSION_POST, descriptor.Name, "ordinary GPU scene submission is disabled in this build");
#endif
	}
}

void ValidateGeometryOcclusionPostOrder(const std::vector<PipelineStage::Descriptor>& stages) {
	const PipelineStage::Descriptor* geometry = nullptr;
	std::set<std::string> writtenAfterGeometry;
	for (const auto& stage : stages) {
		if (!stage.Enabled) {
			continue;
		}
		if (stage.Draw == DrawContent::DEFERRED && stage.Name == GeometryStageName) {
			geometry = &stage;
			writtenAfterGeometry.clear();
			continue;
		}
		if (stage.Draw == DrawContent::VIRTUAL_GEOMETRY_POST || stage.Draw == DrawContent::GPU_SCENE_OCCLUSION_POST) {
			Require(geometry != nullptr, stage.Name, "must follow the enabled Geometry stage");
			Require(stage.FrameBuffer == geometry->FrameBuffer, stage.Name, "must draw into the Geometry stage's FrameBuffer");
			if (stage.Draw == DrawContent::GPU_SCENE_OCCLUSION_POST) {
				Require(!geometry->Once && geometry->UpdateInterval == 1, stage.Name, "Geometry must run each frame");
			}
			const auto& hzb = stage.Bindings.at(HzbBinding);
			Require(writtenAfterGeometry.contains(hzb), stage.Name, "Hzb texture " + hzb + " is not built between Geometry and this stage");
			continue;
		}
		if (stage.Once || stage.UpdateInterval != 1) {
			continue;
		}
		writtenAfterGeometry.insert(stage.Writes.begin(), stage.Writes.end());
		if (!stage.DispatchTexture.empty()) {
			writtenAfterGeometry.insert(stage.DispatchTexture);
		}
	}
}

const PipelineStage* FindActiveGeometryOcclusionPostStage(
	const RenderGraphPipeline& pipeline, DrawContent kind, std::shared_ptr<TextureInterface>& hzb) {
	hzb.reset();
	for (const auto& stage : pipeline.mStages) {
		if (!stage->mDescriptor.Enabled || stage->mDrawContent != kind || !stage->mFrameBuffer) {
			continue;
		}
		const auto found = pipeline.mAllocatedTextures.find(stage->mDescriptor.Bindings.at(HzbBinding));
		if (found == pipeline.mAllocatedTextures.end() || !found->second) {
			return nullptr;
		}
		if (found->second->getWidth() != stage->mFrameBuffer->getWidth() || found->second->getHeight() != stage->mFrameBuffer->getHeight()) {
			return nullptr;
		}
		if (kind == DrawContent::GPU_SCENE_OCCLUSION_POST
			&& (found->second->getFormat() != PixelFormat::R_FLOAT || found->second->getMipCount() < 2)) {
			return nullptr;
		}
		if (kind == DrawContent::GPU_SCENE_OCCLUSION_POST && pipeline.mDescriptor.Graph.Enabled
			&& std::ranges::none_of(pipeline.mGraphPlan.Schedule, [&](const auto& pass) { return pipeline.mStageIndex[pass.Id] == stage.get(); })) {
			return nullptr;
		}
		hzb = found->second;
		return stage.get();
	}
	return nullptr;
}
}
