#include "driverInterface.h"
#include <stdexcept>
#include <unordered_set>

namespace IKIGAI::RENDER {
thread_local DriverInterface* DriverInterface::sReplayDriver = nullptr;

void DriverInterface::requireOwnerThread() const {
	if (sReplayDriver != this) {
		mOwner.requireCurrentThread();
	}
}

std::thread::id DriverInterface::resourceOwnerThread() noexcept {
	return sReplayDriver ? sReplayDriver->mOwner.thread() : std::this_thread::get_id();
}

void DriverInterface::withReplayAccess(const std::function<void()>& work) {
	struct ReplayScope {
		DriverInterface*& Driver;
		DriverInterface* Previous;
		~ReplayScope() {
			Driver = Previous;
		}
	} scope { sReplayDriver, sReplayDriver };
	sReplayDriver = this;
	work();
}

DriverInterface* DriverInterface::Get() {
	if (sActive) {
		sActive->prepareAccess();
	}
	return sActive;
}

void DriverInterface::prepareAccess() {
	requireOwnerThread();
	if (sReplayDriver != this && mAccessBoundary) {
		mAccessBoundary();
	}
}

void DriverInterface::presentFrame() {
	throw std::logic_error("Backend does not support threaded presentation");
}

void DriverInterface::waitForPresentedFrame() {
	throw std::logic_error("Backend does not support threaded presentation");
}

void DriverInterface::finishPresent() {
	throw std::logic_error("Backend does not support threaded presentation");
}

std::function<void()> DriverInterface::presentedFrameWait(GpuCompletionToken) {
	throw std::logic_error("Backend does not support pipelined frame completion");
}

void DriverInterface::renderGraphBarrier(bool) {
	throw std::logic_error("Backend does not support explicit render graph barriers");
}

bool DriverInterface::independentFills(std::span<const BufferFill> fills) {
	if (fills.size() < 2) {
		return false;
	}
	std::unordered_set<const StorageBufferInterface*> buffers;
	for (const auto& fill : fills) {
		if (!fill.Size || !buffers.insert(fill.Buffer.get()).second) {
			return false;
		}
	}
	return true;
}

void DriverInterface::fillBuffers(std::span<const BufferFill> fills) {
	for (const auto& fill : fills) {
		fillBuffer(fill.Buffer, fill.Offset, fill.Size, fill.Value);
	}
}

void DriverInterface::waitForCompletion(GpuCompletionToken token) {
	requireOwnerThread();
	mCompletion->validate(token);
	if (!mCompletion->isComplete(token)) {
		throw std::logic_error("Backend does not implement GPU completion waits");
	}
}
}
