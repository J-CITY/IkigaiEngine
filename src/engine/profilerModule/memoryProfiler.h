#pragma once
#include "gpuMemoryProfiler.h"
#include "processMemory.h"
#include "utilsModule/memory/allocationTracker.h"
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace IKIGAI::PROFILER {
using MEMORY::MemoryAllocation;
using MEMORY::MemoryTotals;

struct MemorySnapshot {
	GpuMemorySnapshot gpu;
	ProcessMemorySnapshot process;
	std::array<MemoryTotals, static_cast<std::size_t>(MEMORY::Category::Count)> categories {};
	std::vector<MemoryAllocation> allocations;
	std::size_t droppedRecords = 0;
};

class MemoryProfiler {
public:
	static MemoryProfiler& instance() {
		static auto* profiler = new MemoryProfiler;
		return *profiler;
	}

	void record(MemoryAllocation allocation) noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		MEMORY::AllocationTracker::instance().record(allocation);
#else
		(void)allocation;
#endif
	}

	std::optional<MemoryAllocation> take(const void* address, const void* allocator) noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		return MEMORY::AllocationTracker::instance().take(address, allocator);
#else
		(void)address;
		(void)allocator;
		return {};
#endif
	}

	void releaseAllocator(const void* allocator) noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		MEMORY::AllocationTracker::instance().releaseAllocator(allocator);
#else
		(void)allocator;
#endif
	}

	MemorySnapshot captureSnapshot() const {
		MemorySnapshot snapshot;
#if IKIGAI_ENABLE_MEMORY_TRACKING
		auto cpu = MEMORY::AllocationTracker::instance().captureSnapshot();
		snapshot.categories = cpu.categories;
		snapshot.allocations = std::move(cpu.allocations);
		snapshot.droppedRecords = cpu.droppedRecords;
		snapshot.gpu = GpuMemoryProfiler::instance().captureSnapshot();
		snapshot.process = CaptureProcessMemory();
#endif
		return snapshot;
	}
};
}
