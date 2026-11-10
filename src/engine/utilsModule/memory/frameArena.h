#pragma once

#include "allocator.h"
#include <memory>

namespace IKIGAI::MEMORY {
class MonotonicAllocator;

// Owner-thread scratch storage. Containers keep the allocation domain alive.
// Join any jobs using its memory before destroying consumers and resetting it.
class FrameArena final {
public:
	explicit FrameArena(
		std::size_t pageBytes = 64 * 1024, std::size_t budget = std::numeric_limits<std::size_t>::max(), std::shared_ptr<IAllocator> upstream = {});
	~FrameArena();
	FrameArena(const FrameArena&) = delete;
	FrameArena& operator=(const FrameArena&) = delete;

	[[nodiscard]] std::shared_ptr<IAllocator> domain() const noexcept;
	[[nodiscard]] static std::shared_ptr<IAllocator> heapDomain();

	// Throws while a consumer owns the domain. Retains pages for the next frame.
	void reset();
	// Allocation count and used bytes are per reset interval. Peak spans all intervals.
	[[nodiscard]] AllocatorStats stats() const noexcept;

private:
	std::shared_ptr<MonotonicAllocator> mArena;
};
}
