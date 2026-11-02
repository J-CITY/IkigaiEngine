#include "chunkModelRenderer.h"
#include "surfaceModule/surfaceRuntime.h"

namespace IKIGAI::ECS {
ChunkModelRenderer::ChunkModelRenderer(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "ChunkModelRenderer";
	apply({});
}
ChunkModelRenderer::ChunkModelRenderer(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ComponentBase(object)
	, mPath(descriptor.Path) {
	__NAME__ = "ChunkModelRenderer";
	apply(descriptor.Voxels);
	if (descriptor.Edits.size() > 1000000) {
		throw std::invalid_argument("Voxel edit budget exceeded");
	}
	for (const auto& edit : descriptor.Edits) {
		mModel->set(edit.X, edit.Y, edit.Z, edit.Type);
	}
}
ChunkModelRenderer::ChunkModelRenderer(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: ChunkModelRenderer(object, static_cast<const Descriptor&>(descriptor)) { }
ChunkModelRenderer::Descriptor ChunkModelRenderer::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<ChunkModelRenderer>();
	descriptor.Path = mPath;
	descriptor.Voxels = settings();
	descriptor.Edits = mModel->edits();
	return descriptor;
}
void ChunkModelRenderer::apply(const SURFACE::VoxelSettings& settings) {
	mModel = std::make_shared<SURFACE::VoxelWorld>(settings);
	frame = {};
	frame.Budget = settings.BuildBudget;
}
const SURFACE::VoxelSettings& ChunkModelRenderer::settings() const {
	return mModel->settings();
}
SURFACE::Block ChunkModelRenderer::getBlock(int x, int y, int z) const {
	return mModel->get(x, y, z);
}
void ChunkModelRenderer::setBlock(int x, int y, int z, SURFACE::Block block) {
	mModel->set(x, y, z, block);
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ChunkModelRenderer>();
	return true;
}();
}
