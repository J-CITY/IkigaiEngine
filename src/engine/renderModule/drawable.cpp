#include "drawable.h"
#include "utilsModule/memory/frameArena.h"

namespace IKIGAI::RENDER {
RenderChunk::RenderChunk(std::shared_ptr<FrameBufferInterface> target, UTILS::WeakPtr<ECS::CameraComponent> viewCamera,
	std::shared_ptr<MEMORY::IAllocator> allocationDomain)
	: frameBuffer(std::move(target))
	, camera(std::move(viewCamera))
	, opaqueDrawablesForward(DrawableAllocator(allocationDomain ? allocationDomain : MEMORY::FrameArena::heapDomain()))
	, transparentDrawablesForward(opaqueDrawablesForward.get_allocator())
	, opaqueDrawablesDeferred(opaqueDrawablesForward.get_allocator())
	, transparentDrawablesDeferred(opaqueDrawablesForward.get_allocator()) { }
}
