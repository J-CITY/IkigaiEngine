#include "vertexBufferVk.h"

#include "driverVk.h"
#include "helpers.h"

#ifdef VULKAN_BACKEND

IKIGAI::RENDER::VertexBufferVk::VertexBufferVk(void* data, size_t size, size_t stride): VertexBufferInterface(size, stride) {
	if (mSizeByte > 0) {
		mBuffer = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, UtilityVk::MemoryUsage::GpuOnly);
	}
	if (data && mSizeByte > 0) {
		VertexBufferVk::setData(data, size, stride);
	}
}

IKIGAI::RENDER::VertexBufferVk::~VertexBufferVk() {
	if (mBuffer) UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
}

void IKIGAI::RENDER::VertexBufferVk::setData(const void* data, size_t sz, size_t stride) {
	UtilityVk::GetDriver()->deactivateRenderPass();

	if (sz * stride > mSizeByte || !mBuffer) {
		mSize = sz;
		mStride = stride;
		mSizeByte = mSize * mStride;
		if (mBuffer) UtilityVk::GetDriver()->destroyDeferred(std::move(mBuffer));
		mBuffer = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, UtilityVk::MemoryUsage::GpuOnly);
	}

	UtilityVk::SetMemoryBarrier(UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer, UtilityVk::GetDriver()->mCurrentMemoryStage, vk::PipelineStageFlagBits2::eTransfer);
	UtilityVk::GetDriver()->mCurrentMemoryStage = vk::PipelineStageFlagBits2::eTransfer;

	if (mSizeByte < 65536) {
		UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.updateBuffer<uint8_t>(*mBuffer, 0, {(uint32_t)mSizeByte, (uint8_t*)data});
		return;
	}

	auto staging_buffer = UtilityVk::CreateBuffer(mSizeByte, vk::BufferUsageFlagBits::eTransferSrc, UtilityVk::MemoryUsage::Staging);
	staging_buffer.upload(data, mSizeByte);

	vk::BufferCopy region;
	region.setSize(mSizeByte);

	UtilityVk::GetDriver()->getCurrentFrame().mCommandBuffer.copyBuffer(*staging_buffer, *mBuffer, {region});

	UtilityVk::GetDriver()->destroyDeferred(std::move(staging_buffer));
}

void IKIGAI::RENDER::VertexBufferVk::bind() {

}

void IKIGAI::RENDER::VertexBufferVk::unbind() {

}

#endif
