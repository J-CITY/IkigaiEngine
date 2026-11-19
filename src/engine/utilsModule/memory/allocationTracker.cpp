#include "allocationTracker.h"

#include <algorithm>

namespace IKIGAI::MEMORY {
AllocationTracker& AllocationTracker::instance() {
	// Use the system allocator for bookkeeping and keep it available during late teardown.
	static auto* tracker = new AllocationTracker;
	return *tracker;
}

void AllocationTracker::record(MemoryAllocation allocation) noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
	try {
		std::lock_guard lock(mMutex);
		if (!mAllocations.emplace(allocation.address, allocation).second) {
			++mDropped;
			return;
		}
		auto& totals = mTotals[static_cast<std::size_t>(allocation.category)];
		totals.bytes += allocation.bytes;
		totals.peakBytes = std::max(totals.peakBytes, totals.bytes);
		++totals.allocations;
	} catch (...) {
		++mDropped;
	}
#else
	(void)allocation;
#endif
}

std::optional<MemoryAllocation> AllocationTracker::take(const void* address, const void* allocator) noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::lock_guard lock(mMutex);
	const auto it = mAllocations.find(address);
	if (it == mAllocations.end() || it->second.allocator != allocator) {
		return {};
	}
	const auto allocation = it->second;
	auto& totals = mTotals[static_cast<std::size_t>(allocation.category)];
	totals.bytes -= allocation.bytes;
	--totals.allocations;
	mAllocations.erase(it);
	return allocation;
#else
	(void)address;
	(void)allocator;
	return {};
#endif
}

void AllocationTracker::releaseAllocator(const void* allocator) noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
	// Arenas release allocations as a group before their addresses can be reused.
	std::lock_guard lock(mMutex);
	for (auto it = mAllocations.begin(); it != mAllocations.end();) {
		if (it->second.allocator != allocator) {
			++it;
			continue;
		}
		auto& totals = mTotals[static_cast<std::size_t>(it->second.category)];
		totals.bytes -= it->second.bytes;
		--totals.allocations;
		it = mAllocations.erase(it);
	}
#else
	(void)allocator;
#endif
}

AllocationSnapshot AllocationTracker::captureSnapshot() const {
	AllocationSnapshot snapshot;
#if IKIGAI_ENABLE_MEMORY_TRACKING
	std::lock_guard lock(mMutex);
	snapshot.categories = mTotals;
	snapshot.droppedRecords = mDropped.load();
	snapshot.allocations.reserve(mAllocations.size());
	for (const auto& [address, allocation] : mAllocations) {
		snapshot.allocations.push_back(allocation);
	}
#endif
	return snapshot;
}
}
