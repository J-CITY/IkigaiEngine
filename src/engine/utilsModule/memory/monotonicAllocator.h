#pragma once
#include "resourceAllocator.h"

namespace IKIGAI::MEMORY {
// Growing arena. All storage is reclaimed together; individual frees are no-ops.
// Destroy consumers before reset/release. This allocator is not synchronized.
// Tracks logical allocations itself when upstream is untracked; otherwise the
// upstream records retained backing pages. Never count both layers as RAM use.
class MonotonicAllocator final : public IAllocator {
	struct Page {
		std::byte* data;
		std::size_t capacity, offset;
	};

public:
	MonotonicAllocator()
		: MonotonicAllocator(64 * 1024) { }
	explicit MonotonicAllocator(std::size_t pageBytes, std::shared_ptr<IAllocator> upstream = std::make_shared<HeapAllocator>(),
		std::size_t budget = std::numeric_limits<std::size_t>::max(), Category category = Category::General)
		: mUpstream(std::move(upstream))
		, mPages(StlAllocator<Page>(mUpstream))
		, mPageBytes(pageBytes)
		, mBudget(budget)
		, mCategory(category)
		, mTrackLogical(mUpstream && !mUpstream->tracksAllocations()) {
		if (!mUpstream || !pageBytes || static_cast<std::size_t>(category) >= static_cast<std::size_t>(Category::Count)) {
			throw std::invalid_argument("Invalid arena configuration");
		}
#if IKIGAI_ENABLE_MEMORY_TRACKING
		(void)PROFILER::MemoryProfiler::instance();
#endif
	}
	~MonotonicAllocator() override {
		release();
	}
	MonotonicAllocator(const MonotonicAllocator&) = delete;
	MonotonicAllocator& operator=(const MonotonicAllocator&) = delete;
	void* allocate(std::size_t bytes) override {
		return allocate(bytes, alignof(std::max_align_t));
	}
	void* allocate(std::size_t bytes, std::size_t alignment) override {
		Detail::ValidateAlignment(alignment);
		const auto size = std::max(bytes, std::size_t { 1 });
		for (std::size_t i = mCurrent; i < mPages.size(); ++i) {
			auto& page = mPages[i];
			const auto padding = Detail::Padding(reinterpret_cast<std::uintptr_t>(page.data + page.offset), alignment);
			if (padding <= page.capacity - page.offset && size <= page.capacity - page.offset - padding) {
				void* result = page.data + page.offset + padding;
				page.offset += padding + size;
				mUsed += padding + size;
				mCurrent = i;
				mPeak = std::max(mPeak, mUsed);
				++mCount;
#if IKIGAI_ENABLE_MEMORY_TRACKING
				if (mTrackLogical) {
					PROFILER::MemoryProfiler::instance().record({ result, this, bytes, alignment, mCategory });
				}
#endif
				return result;
			}
		}
		const auto capacity = std::max(mPageBytes, Detail::CheckedAdd(size, alignment - 1));
		if (capacity > mBudget - mReserved) {
			throw std::bad_alloc();
		}
		auto* data = static_cast<std::byte*>(mUpstream->allocate(capacity, alignment));
		if (!data) {
			throw std::bad_alloc();
		}
		try {
			mPages.push_back({ data, capacity, 0 });
		} catch (...) {
			mUpstream->deallocate(data);
			throw;
		}
		mReserved += capacity;
		mCurrent = mPages.size() - 1;
		return allocate(bytes, alignment);
	}
	void deallocate(void*) override { }
	void reset() noexcept {
#if IKIGAI_ENABLE_MEMORY_TRACKING
		if (mTrackLogical) {
			PROFILER::MemoryProfiler::instance().releaseAllocator(this);
		}
#endif
		for (auto& page : mPages) {
			page.offset = 0;
		}
		mCurrent = mUsed = mCount = 0;
	}
	void release() noexcept {
		reset();
		for (auto& page : mPages) {
			mUpstream->deallocate(page.data);
		}
		mPages.clear();
		mReserved = 0;
	}
	AllocatorStats stats() const noexcept override {
		return { mReserved, mUsed, mPeak, mCount };
	}
	bool tracksAllocations() const noexcept override {
		return IKIGAI_ENABLE_MEMORY_TRACKING != 0;
	}

private:
	std::shared_ptr<IAllocator> mUpstream;
	std::vector<Page, StlAllocator<Page>> mPages;
	std::size_t mPageBytes, mBudget, mReserved = 0, mUsed = 0, mPeak = 0, mCount = 0, mCurrent = 0;
	Category mCategory;
	const bool mTrackLogical;
};

// Each thread/task/frame owns a separate instance. Outstanding owning adapters
// prevent premature reset; destroying the owner lets outstanding consumers finish.
class ScratchMemory {
public:
	ScratchMemory()
		: ScratchMemory(64 * 1024) { }
	explicit ScratchMemory(std::size_t pageBytes, std::size_t budget = std::numeric_limits<std::size_t>::max())
		: mArena(std::make_shared<MonotonicAllocator>(pageBytes, std::make_shared<HeapAllocator>(), budget)) { }
	ScratchMemory(const ScratchMemory&) = delete;
	ScratchMemory& operator=(const ScratchMemory&) = delete;
	template <class T> StlAllocator<T> allocator() const {
		return StlAllocator<T>(mArena);
	}
	void reset() {
		if (mArena.use_count() != 1) {
			throw std::logic_error("Scratch consumers must be destroyed before reset");
		}
		mArena->reset();
	}
	AllocatorStats stats() const {
		return mArena->stats();
	}

private:
	std::shared_ptr<MonotonicAllocator> mArena;
};
}
