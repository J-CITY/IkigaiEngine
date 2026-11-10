#include "uniformBufferVk.h"

#ifdef VULKAN_BACKEND
#include "helpers.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace IKIGAI::RENDER {
UniformBufferVk::UniformBufferVk(void* data, size_t size)
	: UniformBufferInterface(size)
	, mDriver(*UtilityVk::GetDriver())
	, mBytes(size) {
	if (data && size) {
		std::memcpy(mBytes.data(), data, size);
	}
	if (size) {
		snapshot();
	}
}

UniformBufferVk::~UniformBufferVk() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
	for (const auto& snapshot : mSnapshots) {
		if (snapshot.Native.use_count() > 1) {
			snapshot.Native->Buffer.markPendingRelease();
		}
	}
#endif
}

const VmaBuffer& UniformBufferVk::getBuffer() const {
	if (mSnapshots.empty()) {
		return mEmpty;
	}
	const auto& snapshot = mSnapshots[mSnapshot].Native;
	mDriver.retainUniformSnapshot(snapshot);
	return snapshot->Buffer;
}

void UniformBufferVk::setData(const void* data, size_t size, size_t offset) {
	requireOwnerThread();
	if (!size) {
		return;
	}
	if (!data || offset > std::numeric_limits<size_t>::max() - size) {
		throw std::invalid_argument("Invalid uniform buffer update");
	}
	mSizeByte = std::max(mSizeByte, offset + size);
	mBytes.resize(mSizeByte);
	std::memcpy(mBytes.data() + offset, data, size);
	snapshot();
}

void UniformBufferVk::snapshot() {
	// Identical bytes can share a snapshot even while the GPU is reading it.
	const auto identical = std::find_if(mSnapshots.begin(), mSnapshots.end(), [&](const Snapshot& value) { return value.Bytes == mBytes; });
	if (identical != mSnapshots.end()) {
		mSnapshot = static_cast<size_t>(identical - mSnapshots.begin());
		return;
	}
	// A recording or submitted frame retains the native snapshot until retirement.
	auto slot = std::find_if(mSnapshots.begin(), mSnapshots.end(), [](const Snapshot& value) { return value.Native.use_count() == 1; });
	if (slot == mSnapshots.end()) {
		mSnapshots.push_back({ std::make_shared<UniformSnapshotVk>(), {} });
		slot = std::prev(mSnapshots.end());
	}
	auto& buffer = slot->Native->Buffer;
	if (!buffer || slot->Bytes.size() != mBytes.size()) {
		auto replacement = UtilityVk::CreateBuffer(mBytes.size(), vk::BufferUsageFlagBits::eUniformBuffer, UtilityVk::MemoryUsage::Staging);
		replacement.upload(mBytes.data(), mBytes.size());
		buffer = std::move(replacement);
	}
	else {
		buffer.upload(mBytes.data(), mBytes.size());
	}
	slot->Bytes = mBytes;
	mSnapshot = static_cast<size_t>(slot - mSnapshots.begin());
}
}
#endif
