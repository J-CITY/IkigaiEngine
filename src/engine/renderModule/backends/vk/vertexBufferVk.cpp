#include "vertexBufferVk.h"
#ifdef VULKAN_BACKEND
#include "bufferUploadVk.h"
#include "driverVk.h"
#include "helpers.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
VertexBufferVk::VertexBufferVk(void* data, size_t count, size_t stride)
	: VertexBufferInterface(0, stride) {
	VertexBufferVk::setData(data, count, stride);
}
VertexBufferVk::~VertexBufferVk() {
	if (mBuffer) {
		UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
	}
}
void VertexBufferVk::setData(const void* data, size_t count, size_t stride) {
	requireOwnerThread();
	const size_t bytes = CheckedBufferSizeVk(count, stride);
	if (!bytes) {
		return;
	}
	ReserveBufferVk(mBuffer, mCapacityByte, bytes, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
	mSize = count;
	mStride = stride;
	mSizeByte = bytes;
	if (data) {
		UploadBufferVk(mBuffer, data, bytes);
	}
}
void VertexBufferVk::bind() { }
void VertexBufferVk::unbind() { }
}
#endif
