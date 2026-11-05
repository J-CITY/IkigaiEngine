#include "indexBufferVk.h"
#ifdef VULKAN_BACKEND
#include "bufferUploadVk.h"
#include "driverVk.h"
#include "helpers.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
IndexBufferVk::IndexBufferVk(void* data, size_t count, size_t stride)
	: IndexBufferInterface(0, stride) {
	IndexBufferVk::setData(data, count, stride);
}
IndexBufferVk::~IndexBufferVk() {
	if (mBuffer) {
		UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
	}
}
void IndexBufferVk::setData(const void* data, size_t count, size_t stride) {
	requireOwnerThread();
	const size_t bytes = CheckedBufferSizeVk(count, stride);
	if (!bytes) {
		return;
	}
	ReserveBufferVk(mBuffer, mCapacityByte, bytes, vk::BufferUsageFlagBits::eIndexBuffer);
	mSize = count;
	mStride = stride;
	mSizeByte = bytes;
	if (data) {
		UploadBufferVk(mBuffer, data, bytes);
	}
}
void IndexBufferVk::bind() { }
void IndexBufferVk::unbind() { }
}
#endif
