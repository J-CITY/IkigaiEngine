#include "weakPtr.h"

using namespace IKIGAI::UTILS;

namespace {
void releaseControlBlock(ControlBlock* block, ControlBlockHandler* owner) {
	if (!block) {
		return;
	}
	if (block->mPtr == owner) {
		block->mPtr = nullptr;
	}
	if (block->mRC.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		delete block;
	}
}
}

ControlBlockHandler::ControlBlockHandler() {
	mCb = new UTILS::ControlBlock();
	mCb->mRC.fetch_add(1, std::memory_order_relaxed);
	mCb->mPtr = this;
}

ControlBlockHandler::ControlBlockHandler(ECS::Component* ptr) {
	mCb = new UTILS::ControlBlock();
	mCb->mRC.fetch_add(1, std::memory_order_relaxed);
	mCb->mPtr = (ControlBlockHandler*)ptr;
}

ControlBlockHandler::ControlBlockHandler(const ControlBlockHandler& obj) {
	// Component's destructor suppresses implicit move, so arrays relocate by copy.
	// The copy keeps the block and becomes its current address. A fresh block would
	// leave existing weak pointers on the source, which then dies.
	mCb = obj.mCb;
	if (mCb) {
		mCb->mRC.fetch_add(1, std::memory_order_relaxed);
		mCb->mPtr = this;
	}
}

ControlBlockHandler& ControlBlockHandler::operator=(const ControlBlockHandler& obj) {
	if (this == &obj) {
		return *this;
	}
	releaseControlBlock(mCb, this);
	mCb = obj.mCb;
	if (mCb) {
		mCb->mRC.fetch_add(1, std::memory_order_relaxed);
		mCb->mPtr = this;
	}
	return *this;
}

ControlBlockHandler::ControlBlockHandler(ControlBlockHandler&& obj) noexcept {
	mCb = obj.mCb;
	obj.mCb = nullptr;
}

ControlBlockHandler& ControlBlockHandler::operator=(ControlBlockHandler&& obj) noexcept {
	if (this == &obj) {
		return *this;
	}
	releaseControlBlock(mCb, this);
	mCb = obj.mCb;
	obj.mCb = nullptr;
	return *this;
}

ControlBlockHandler::~ControlBlockHandler() {
	releaseControlBlock(mCb, this);
}

ControlBlock* ControlBlockHandler::getControlBlock() {
	return mCb;
}
