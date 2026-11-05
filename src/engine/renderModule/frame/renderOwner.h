#pragma once
#include <thread>

namespace IKIGAI::RENDER {
// Backend ownership is fixed before recording starts; coroutines do not transfer it.
class RenderOwner final {
public:
	RenderOwner();
	void requireCurrentThread() const;
	[[nodiscard]] bool isCurrentThread() const noexcept;

private:
	std::thread::id mThread;
};
}
