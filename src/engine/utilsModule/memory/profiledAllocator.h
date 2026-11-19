#pragma once
#include "allocationTracker.h"
#include "allocator.h"
#include <utility>

namespace IKIGAI::MEMORY {
template <class Allocator, Category Tag = Category::General> class ProfiledAllocator final : public IAllocator {
public:
	template <class... Args>
	explicit ProfiledAllocator(Args&&... args)
		: mAllocator(std::forward<Args>(args)...) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		// Initialize bookkeeping before the first successful user allocation.
		(void)AllocationTracker::instance();
#endif
	}
	ProfiledAllocator(const ProfiledAllocator&) = delete;
	ProfiledAllocator& operator=(const ProfiledAllocator&) = delete;
	void* allocate(std::size_t bytes) override {
		void* ptr = mAllocator.allocate(bytes);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		// Zero means policy-default alignment, rather than an explicit request.
		if (ptr) {
			AllocationTracker::instance().record({ ptr, this, bytes, 0, Tag });
		}
#endif
		return ptr;
	}
	void* allocate(std::size_t bytes, std::size_t alignment) override {
		void* ptr = static_cast<IAllocator&>(mAllocator).allocate(bytes, alignment);
#if IKIGAI_ENABLE_MEMORY_TRACKING
		if (ptr) {
			AllocationTracker::instance().record({ ptr, this, bytes, alignment, Tag });
		}
#endif
		return ptr;
	}
	void deallocate(void* ptr) override {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		// Remove before releasing storage so another allocator/thread cannot
		// reuse the address while the old record is still present.
		auto& tracker = AllocationTracker::instance();
		auto record = tracker.take(ptr, this);
		try {
			mAllocator.deallocate(ptr);
		} catch (...) {
			if (record) {
				tracker.record(*record);
			}
			throw;
		}
#else
		mAllocator.deallocate(ptr);
#endif
	}
	AllocatorStats stats() const {
		return mAllocator.stats();
	}
	bool tracksAllocations() const noexcept override {
		return IKIGAI_ENABLE_MEMORY_TRACKING != 0;
	}

private:
	Allocator mAllocator;
};

#if IKIGAI_ENABLE_MEMORY_TRACKING
template <class Allocator, Category Tag> using OptionalProfiledAllocator = ProfiledAllocator<Allocator, Tag>;
#else
template <class Allocator, Category Tag> using OptionalProfiledAllocator = Allocator;
#endif
}
