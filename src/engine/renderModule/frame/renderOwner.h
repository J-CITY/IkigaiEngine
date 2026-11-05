#pragma once
#include <thread>

namespace IKIGAI::RENDER {
// Identifies the producer thread. Coroutines do not transfer its ownership.
class RenderOwner final {
public:
	RenderOwner();
	void requireCurrentThread() const;
	[[nodiscard]] bool isCurrentThread() const noexcept;
	[[nodiscard]] std::thread::id thread() const noexcept;

private:
	std::thread::id mThread;
};
}
