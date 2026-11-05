#include "uniformBufferVk.h"


#include "driverVk.h"
#include "helpers.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>

#ifdef VULKAN_BACKEND

IKIGAI::RENDER::UniformBufferVk::UniformBufferVk(void* data, size_t size) : UniformBufferInterface(size) {
    mBytes.resize(size);
    if (data && size) std::memcpy(mBytes.data(), data, size);
    if (size) snapshot();
}

IKIGAI::RENDER::UniformBufferVk::~UniformBufferVk() {
    for (auto& value : mSnapshots)
        if (value.buffer) UtilityVk::GetDriver()->destroyDeferred(std::move(value.buffer));
}

void IKIGAI::RENDER::UniformBufferVk::setData(const void* data, size_t sz, size_t offset) {
	requireOwnerThread();
	if (!sz) {
		return;
	}
	if (!data || offset > std::numeric_limits<size_t>::max() - sz) {
		throw std::invalid_argument("Invalid uniform buffer update");
	}
	mSizeByte = std::max(mSizeByte, offset + sz);
	mBytes.resize(mSizeByte);
	std::memcpy(mBytes.data() + offset, data, sz);
	snapshot();
}

void IKIGAI::RENDER::UniformBufferVk::snapshot() {
    // Host writes are flushed by VMA and made visible by queue submission.
    // Distinct buffers preserve per-draw values without vkCmdUpdateBuffer,
    // transfer barriers, or ending dynamic rendering for each material bind.
    for (size_t i = 0; i < mSnapshots.size(); ++i) {
        if (mSnapshots[i].bytes == mBytes) {
            mSnapshot = i;
            mSnapshots[i].usedFrame = sFrame;
            return;
        }
    }
    auto slot = std::find_if(mSnapshots.begin(), mSnapshots.end(), [](const Snapshot& value) {
        return value.usedFrame != sFrame;
    });
    if (slot == mSnapshots.end()) {
        mSnapshots.emplace_back();
        slot = std::prev(mSnapshots.end());
    }
    if (!slot->buffer || slot->bytes.size() != mBytes.size()) {
        if (slot->buffer) UtilityVk::GetDriver()->destroyDeferred(std::move(slot->buffer));
        slot->buffer = UtilityVk::CreateBuffer(mBytes.size(), vk::BufferUsageFlagBits::eUniformBuffer,
            UtilityVk::MemoryUsage::Staging);
    }
    slot->buffer.upload(mBytes.data(), mBytes.size());
    slot->bytes = mBytes;
    slot->usedFrame = sFrame;
    mSnapshot = static_cast<size_t>(slot - mSnapshots.begin());
}

#endif
