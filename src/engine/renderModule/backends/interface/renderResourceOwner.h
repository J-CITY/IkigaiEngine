#pragma once
#include "renderModule/frame/renderOwner.h"

namespace IKIGAI::RENDER {
// Legacy direct mutation is a serial owner-thread boundary. Prefer Renderer uploads,
// which copy producer bytes and preserve their position in the command stream.
class RenderResourceOwner {
protected:
	void requireOwnerThread() const;

private:
	RenderOwner mOwner {};
};
}
