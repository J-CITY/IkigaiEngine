#pragma once

#include "allocator.h"

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#ifndef IKIGAI_ENABLE_MEMORY_TRACKING
#define IKIGAI_ENABLE_MEMORY_TRACKING 0
#endif

namespace IKIGAI::MEMORY {
struct MemoryAllocation {
	const void* address = nullptr;
	const void* allocator = nullptr;
	std::size_t bytes = 0;
	std::size_t alignment = 0;
	Category category = Category::General;
};

struct MemoryTotals {
	std::size_t bytes = 0;
	std::size_t peakBytes = 0;
	std::size_t allocations = 0;
};

struct AllocationSnapshot {
	std::array<MemoryTotals, static_cast<std::size_t>(Category::Count)> categories {};
	std::vector<MemoryAllocation> allocations;
	std::size_t droppedRecords = 0;
};

class AllocationTracker {
public:
	static AllocationTracker& instance();

	void record(MemoryAllocation allocation) noexcept;
	std::optional<MemoryAllocation> take(const void* address, const void* allocator) noexcept;
	void releaseAllocator(const void* allocator) noexcept;
	AllocationSnapshot captureSnapshot() const;

private:
	AllocationTracker() = default;
	AllocationTracker(const AllocationTracker&) = delete;
	AllocationTracker& operator=(const AllocationTracker&) = delete;

#if IKIGAI_ENABLE_MEMORY_TRACKING
	mutable std::mutex mMutex;
	std::unordered_map<const void*, MemoryAllocation> mAllocations;
	std::array<MemoryTotals, static_cast<std::size_t>(Category::Count)> mTotals {};
	std::atomic<std::size_t> mDropped = 0;
#endif
};
}
