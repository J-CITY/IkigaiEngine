#include "frameArena.h"

#include "monotonicAllocator.h"

namespace IKIGAI::MEMORY {
FrameArena::FrameArena(std::size_t pageBytes, std::size_t budget, std::shared_ptr<IAllocator> upstream)
	: mArena(std::make_shared<MonotonicAllocator>(
		  pageBytes, upstream ? std::move(upstream) : std::make_shared<OptionalProfiledAllocator<HeapAllocator, Category::General>>(), budget)) { }

FrameArena::~FrameArena() = default;

std::shared_ptr<IAllocator> FrameArena::domain() const noexcept {
	return mArena;
}

std::shared_ptr<IAllocator> FrameArena::heapDomain() {
	static const auto domain = std::make_shared<HeapAllocator>();
	return domain;
}

void FrameArena::reset() {
	if (mArena.use_count() != 1) {
		throw std::logic_error("Frame arena consumers must be destroyed before reset");
	}
	mArena->reset();
}

AllocatorStats FrameArena::stats() const noexcept {
	return mArena->stats();
}
}
