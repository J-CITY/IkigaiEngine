#include "weakPtr.h"

using namespace IKIGAI::UTILS;

ControlBlockHandler::ControlBlockHandler(const ControlBlockHandler&) noexcept { }

ControlBlockHandler& ControlBlockHandler::operator=(const ControlBlockHandler&) noexcept {
	return *this;
}

ControlBlockHandler::ControlBlockHandler(ControlBlockHandler&&) noexcept { }

ControlBlockHandler& ControlBlockHandler::operator=(ControlBlockHandler&&) noexcept {
	return *this;
}

ControlBlockHandler::~ControlBlockHandler() {
	invalidateWeakReferences();
}

ControlBlock* ControlBlockHandler::getControlBlock() {
	auto* block = mCb.load(std::memory_order_acquire);
	if (!block) {
		auto candidate = std::make_unique<ControlBlock>(this);
		if (mCb.compare_exchange_strong(block, candidate.get(), std::memory_order_acq_rel)) {
			return candidate.release();
		}
	}
	return block;
}

void ControlBlockHandler::swapControlBlocks(ControlBlockHandler& other) noexcept {
	// ECS storage changes happen outside parallel iteration.
	auto* block = mCb.load(std::memory_order_relaxed);
	auto* otherBlock = other.mCb.exchange(block, std::memory_order_relaxed);
	mCb.store(otherBlock, std::memory_order_relaxed);
	if (block) {
		block->mPtr.store(&other, std::memory_order_release);
	}
	if (otherBlock) {
		otherBlock->mPtr.store(this, std::memory_order_release);
	}
}

void ControlBlockHandler::invalidateWeakReferences() noexcept {
	auto* block = mCb.exchange(nullptr, std::memory_order_relaxed);
	if (!block) {
		return;
	}
	block->mPtr.store(nullptr, std::memory_order_release);
	if (block->mRC.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete block;
	}
}
