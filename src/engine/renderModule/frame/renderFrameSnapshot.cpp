#include "renderFrameSnapshot.h"
#include <utility>

namespace IKIGAI::RENDER {
RenderFrameSnapshot::RenderFrameSnapshot(uint64_t frameId, std::vector<RenderViewSnapshot> views)
	: mFrameId(frameId)
	, mViews(std::move(views)) { }

uint64_t RenderFrameSnapshot::frameId() const noexcept {
	return mFrameId;
}

const std::vector<RenderViewSnapshot>& RenderFrameSnapshot::views() const noexcept {
	return mViews;
}
}
