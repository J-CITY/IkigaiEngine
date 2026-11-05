#pragma once
#include <thread>

namespace IKIGAI::RENDER {
// Legacy direct mutation is a serial owner-thread boundary. Prefer Renderer uploads,
// which copy producer bytes and preserve their position in the command stream.
class RenderResourceOwner {
protected:
	RenderResourceOwner();
	void requireOwnerThread() const;

private:
	std::thread::id mOwner;
};
}
