#pragma once
#include "renderCommand.h"
#include <memory>

namespace IKIGAI::RENDER {
// Owner-thread CPU packets. Replay does not imply GPU completion.
class RenderCommandBuffer final {
public:
	using Construct = CommandHeader* (*)(void* storage, void* context);
	using Destroy = void (*)(CommandHeader*) noexcept;

	explicit RenderCommandBuffer(size_t blockSize = 64 * 1024, bool retainExecuted = false);
	~RenderCommandBuffer();
	RenderCommandBuffer(const RenderCommandBuffer&) = delete;
	RenderCommandBuffer& operator=(const RenderCommandBuffer&) = delete;

	// Construction or dispatch failure cancels the complete pending batch.
	void append(DriverInterface* driver, size_t size, size_t alignment, Construct construct, void* context, Destroy destroy);
	void execute(DriverInterface* driver, CommandDispatch dispatch);
	// During replay, cancel stops after the current packet without destroying it early.
	void cancel() noexcept;
	// Frame owner calls reset only after GPU completion or abandoning the native recording.
	void reset() noexcept;
	void seal();
	[[nodiscard]] bool isExecuting() const noexcept;
	[[nodiscard]] bool retainsExecuted() const noexcept;
	[[nodiscard]] size_t pendingCommandCount() const noexcept;
	// Pages are retained at the high-water mark for reuse, and freed on destruction.
	[[nodiscard]] size_t reservedBytes() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> mImpl;
};
}
