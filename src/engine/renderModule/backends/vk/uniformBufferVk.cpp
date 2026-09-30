#include "uniformBufferVk.h"


#include "driverVk.h"
#include "helpers.h"

#ifdef VULKAN_BACKEND

IKIGAI::RENDER::UniformBufferVk::UniformBufferVk(void* data, size_t size) : UniformBufferInterface(size) {
	std::tie(mBuffer, mDeviceMemory) = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst);
	if (data) {
		//UniformBufferVk::setData(data, size);
	}
}

IKIGAI::RENDER::UniformBufferVk::~UniformBufferVk() {
	UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
	UtilityVk::GetDriver()->destroyDeferred(std::move(mDeviceMemory));
}

void IKIGAI::RENDER::UniformBufferVk::setData(const void* data, size_t sz, size_t offset) {
	UtilityVk::GetDriver()->deactivateRenderPass();

	if (sz > mSizeByte) {
		mSizeByte = sz;
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

#endif
