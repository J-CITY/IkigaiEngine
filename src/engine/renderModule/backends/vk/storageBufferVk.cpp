#include "storageBufferVk.h"

#include "driverVk.h"
#include "helpers.h"

#ifdef VULKAN_BACKEND

IKIGAI::RENDER::StorageBufferVk::StorageBufferVk(void* data, size_t size, size_t stride) : StorageBufferInterface(size, stride) {
	std::tie(mBuffer, mDeviceMemory) = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
	if (data) {
		StorageBufferVk::setData(data, size, stride);
	}
}

IKIGAI::RENDER::StorageBufferVk::~StorageBufferVk() {
	UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
	UtilityVk::GetDriver()->destroyDeferred(std::move(mDeviceMemory));
}

void IKIGAI::RENDER::StorageBufferVk::setData(const void* data, size_t sz, size_t stride) {
	UtilityVk::GetDriver()->deactivateRenderPass();

	if (sz * stride > mSizeByte) {
		mSize = sz;
		mStride = stride;
		mSizeByte = mSize * mStride;
		UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
		UtilityVk::GetDriver()->destroyDeferred(std::move(mDeviceMemory));
		std::tie(mBuffer, mDeviceMemory) = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);
	}

	if (UtilityVk::GetDriver()->mCurrentMemoryStage != vk::PipelineStageFlagBits2::eTransfer) {
		UtilityVk::SetMemoryBarrier(UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer, UtilityVk::GetDriver()->mCurrentMemoryStage, vk::PipelineStageFlagBits2::eTransfer);
		UtilityVk::GetDriver()->mCurrentMemoryStage = vk::PipelineStageFlagBits2::eTransfer;
	}

	if (mSizeByte < 65536) {
		//std::cout << "vkCmdUpdateBuffer\n";
		UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.updateBuffer<uint8_t>(*mBuffer, 0, {(uint32_t)mSizeByte, (uint8_t*)data});
		return;
	}

	auto [staging_buffer, staging_buffer_memory] = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eTransferSrc);

	auto ptr = staging_buffer_memory.mapMemory(0, mSizeByte);
	memcpy(ptr, data, mSizeByte);
	staging_buffer_memory.unmapMemory();

	vk::BufferCopy region;
	region.setSize(mSizeByte);

	UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.copyBuffer(*staging_buffer, *mBuffer, {region});

	UtilityVk::GetDriver()->destroyDeferred(std::move(staging_buffer));
	UtilityVk::GetDriver()->destroyDeferred(std::move(staging_buffer_memory));
}

void IKIGAI::RENDER::StorageBufferVk::setSubData(const void* data, size_t sz, size_t offset) {
	UtilityVk::GetDriver()->deactivateRenderPass();

	if (UtilityVk::GetDriver()->mCurrentMemoryStage != vk::PipelineStageFlagBits2::eTransfer) {
		UtilityVk::SetMemoryBarrier(UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer, UtilityVk::GetDriver()->mCurrentMemoryStage, vk::PipelineStageFlagBits2::eTransfer);
		UtilityVk::GetDriver()->mCurrentMemoryStage = vk::PipelineStageFlagBits2::eTransfer;
	}

	if (sz < 65536) {
		UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.updateBuffer<uint8_t>(*mBuffer, offset, {(uint32_t)sz, (uint8_t*)data});
		return;
	}

	auto [staging_buffer, staging_buffer_memory] = UtilityVk::CreateBuffer(sz, vk::BufferUsageFlagBits::eTransferSrc);

	auto ptr = staging_buffer_memory.mapMemory(0, sz);
	memcpy(ptr, data, sz);
	staging_buffer_memory.unmapMemory();

	vk::BufferCopy region;
    region.setSrcOffset(0);
    region.setDstOffset(offset);
	region.setSize(sz);

	UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.copyBuffer(*staging_buffer, *mBuffer, {region});

	UtilityVk::GetDriver()->destroyDeferred(std::move(staging_buffer));
	UtilityVk::GetDriver()->destroyDeferred(std::move(staging_buffer_memory));
}

void IKIGAI::RENDER::StorageBufferVk::bind() {

}

void IKIGAI::RENDER::StorageBufferVk::unbind() {

}

#endif
