#include "renderOwner.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
RenderOwner::RenderOwner()
	: mThread(std::this_thread::get_id()) { }

bool RenderOwner::isCurrentThread() const noexcept {
	return mThread == std::this_thread::get_id();
}

std::thread::id RenderOwner::thread() const noexcept {
	return mThread;
}

void RenderOwner::requireCurrentThread() const {
	if (!isCurrentThread()) {
		throw std::logic_error("Render backend access from a non-owning thread");
	}
}
}
