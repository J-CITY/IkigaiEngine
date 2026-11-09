#include "driverInterface.h"
#include "utilsModule/crashReport.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace IKIGAI::RENDER {
namespace {
	class GpuWaitWatchdog {
	public:
		bool arm(const char* message) {
			std::lock_guard lock(mMutex);
			if (mMessage) {
				return false;
			}
			mMessage = message;
			mDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
			++mGeneration;
			mWake.notify_one();
			return true;
		}

		void disarm() {
			std::lock_guard lock(mMutex);
			mMessage = nullptr;
			++mGeneration;
			mWake.notify_one();
		}

	private:
		void run(std::stop_token stop) {
			std::unique_lock lock(mMutex);
			while (mWake.wait(lock, stop, [&] { return mMessage != nullptr; }) && !stop.stop_requested()) {
				const auto generation = mGeneration;
				const auto deadline = mDeadline;
				if (!mWake.wait_until(lock, stop, deadline, [&] { return mGeneration != generation; }) && !stop.stop_requested()) {
					UTILS::CRASH::terminate(mMessage);
				}
			}
		}

		std::mutex mMutex;
		std::condition_variable_any mWake;
		const char* mMessage = nullptr;
		std::chrono::steady_clock::time_point mDeadline {};
		uint64_t mGeneration = 0;
		std::jthread mThread { [this](std::stop_token stop) { run(stop); } };
	};

	GpuWaitWatchdog& gpuWaitWatchdog() {
		try {
			thread_local GpuWaitWatchdog watchdog;
			return watchdog;
		} catch (const std::exception&) {
			UTILS::CRASH::terminate("Could not start the GPU wait watchdog");
		}
	}
}

BindlessResourceHandle BindlessResourceTable::add(uint32_t binding, BindlessResourceEntry value, uint32_t limit) {
	if (!value.texture && !value.buffer) {
		throw std::invalid_argument("Null bindless resource");
	}
	if (!capacity) {
		capacity = limit;
	}
	if (!limit || capacity != limit) {
		throw std::invalid_argument("Bindless table capacity changed");
	}
	if (!entries.empty()) {
		const auto& fallback = entries.front();
		if (bool(fallback.texture) != bool(value.texture) || (value.texture && fallback.texture->getType() != value.texture->getType())) {
			throw std::invalid_argument("Bindless resource type differs from table default");
		}
	}
	for (uint32_t index = 0; index < entries.size(); ++index) {
		if (entries[index].texture == value.texture && entries[index].buffer == value.buffer) {
			return { binding, index, entries[index].generation };
		}
	}
	if (!reusableIndices.empty()) {
		const auto index = reusableIndices.back();
		reusableIndices.pop_back();
		value.generation = entries[index].generation;
		entries[index] = std::move(value);
		++revision;
		return { binding, index, entries[index].generation };
	}
	if (entries.size() >= capacity) {
		throw std::runtime_error("Bindless table capacity exhausted");
	}
	const auto index = static_cast<uint32_t>(entries.size());
	entries.push_back(std::move(value));
	++revision;
	return { binding, index, entries.back().generation };
}

const BindlessResourceEntry& BindlessResourceTable::get(uint32_t index) const {
	if (entries.empty()) {
		throw std::runtime_error("Bindless table needs a default resource at slot zero");
	}
	if (index >= entries.size() || (!entries[index].texture && !entries[index].buffer)) {
		return entries.front();
	}
	return entries[index];
}

void BindlessResourceTable::release(BindlessResourceHandle handle) {
	if (!handle.index) {
		throw std::invalid_argument("Cannot release bindless default slot");
	}
	if (handle.index >= entries.size() || entries[handle.index].generation != handle.generation
		|| (!entries[handle.index].texture && !entries[handle.index].buffer)) {
		throw std::invalid_argument("Stale bindless resource handle");
	}
	auto& entry = entries[handle.index];
	entry.texture.reset();
	entry.buffer.reset();
	if (entry.generation != std::numeric_limits<uint32_t>::max()) {
		++entry.generation;
	}
	++revision;
}

void BindlessResourceTable::recycle(BindlessResourceHandle handle) {
	if (!handle.index || handle.index >= entries.size() || handle.generation == std::numeric_limits<uint32_t>::max()) {
		return;
	}
	const auto& entry = entries[handle.index];
	if (entry.generation != handle.generation + 1 || entry.texture || entry.buffer
		|| std::find(reusableIndices.begin(), reusableIndices.end(), handle.index) != reusableIndices.end()) {
		return;
	}
	reusableIndices.push_back(handle.index);
}

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

void DriverInterface::renderGraphImageBarrier(TextureInterface&, const GraphImageBarrier&) {
	throw std::logic_error("Backend does not support render graph image barriers");
}

void DriverInterface::dispatchMipCompute(uint32_t x, uint32_t y, uint32_t z, const std::shared_ptr<TextureInterface>&, uint32_t, uint32_t) {
	dispatch(x, y, z);
}

void DriverInterface::renderGraphIndirectBarrier(const std::shared_ptr<StorageBufferInterface>&) {
	throw std::logic_error("Backend does not support render graph indirect barriers");
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

void DriverInterface::drainForShutdown() {
	waitForCompletion(completion().lastSubmitted());
	if (mFrameGeneration) {
		mFrameGeneration->waitForPresents();
	}
}

[[noreturn]] void DriverInterface::rethrowRenderException(std::exception_ptr error) {
	std::rethrow_exception(error);
}

void DriverInterface::watchGpuWait(const char* timeoutMessage, const std::function<void()>& wait) {
	auto& watchdog = gpuWaitWatchdog();
	struct ScopedWait {
		GpuWaitWatchdog& Watchdog;
		bool Armed;
		~ScopedWait() {
			if (Armed) {
				Watchdog.disarm();
			}
		}
	} scope { watchdog, watchdog.arm(timeoutMessage) };
	// Keep the native wait on the queue owner, including SDK thread affinity.
	wait();
}
}
