#include "driverInterface.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace IKIGAI::RENDER {
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
}
