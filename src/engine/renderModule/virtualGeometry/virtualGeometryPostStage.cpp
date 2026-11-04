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
			throw std::runtime_error("VIRTUAL_GEOMETRY_POST stage " + stage + ": " + message);
		}
	}
}

void ValidateVirtualGeometryPostStage(const PipelineStage::Descriptor& descriptor) {
	Require(descriptor.Bindings.contains(HzbBinding), descriptor.Name, "missing binding Hzb");
	Require(!descriptor.FrameBuffer.empty(), descriptor.Name, "requires the G-buffer FrameBuffer");
	Require(descriptor.Material.empty() && descriptor.Program.empty(), descriptor.Name, "does not accept Material or Program");
	Require(!descriptor.Once && descriptor.UpdateInterval == 1, descriptor.Name, "must run every frame");
	Require(!descriptor.ClearColor && !descriptor.ClearDepth, descriptor.Name, "must not clear the G-buffer (ClearColor/ClearDepth false)");
	Require(descriptor.Unsupported == "BYPASS" || descriptor.Unsupported == "ERROR", descriptor.Name, "Unsupported must be BYPASS or ERROR");
	if (descriptor.Enabled && descriptor.Unsupported == "ERROR") {
		Require(VG::IsVirtualGeometryGpuPathSupported(), descriptor.Name, "the GPU virtual geometry path is unavailable on this backend");
	}
}

void ValidateVirtualGeometryPostOrder(const std::vector<PipelineStage::Descriptor>& stages) {
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
		if (stage.Draw == DrawContent::VIRTUAL_GEOMETRY_POST) {
			Require(geometry != nullptr, stage.Name, "must follow the enabled Geometry stage");
			Require(stage.FrameBuffer == geometry->FrameBuffer, stage.Name, "must draw into the Geometry stage's FrameBuffer");
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

const PipelineStage* FindActiveVirtualGeometryPostStage(const RenderGraphPipeline& pipeline, std::shared_ptr<TextureInterface>& hzb) {
	hzb.reset();
	for (const auto& stage : pipeline.mStages) {
		if (!stage->mDescriptor.Enabled || stage->mDrawContent != DrawContent::VIRTUAL_GEOMETRY_POST || !stage->mFrameBuffer) {
			continue;
		}
		const auto found = pipeline.mAllocatedTextures.find(stage->mDescriptor.Bindings.at(HzbBinding));
		if (found == pipeline.mAllocatedTextures.end() || !found->second) {
			return nullptr;
		}
		if (found->second->getWidth() != stage->mFrameBuffer->getWidth() || found->second->getHeight() != stage->mFrameBuffer->getHeight()) {
			return nullptr;
		}
		hzb = found->second;
		return stage.get();
	}
	return nullptr;
}
}
