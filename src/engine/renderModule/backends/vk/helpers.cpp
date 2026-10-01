#include "helpers.h"


#ifdef VULKAN_BACKEND

#include "driverVk.h"
#include <stdexcept>
#include "../interface/driverInterface.h"
#include <resourceModule/serviceManager.h>

using namespace IKIGAI;
using namespace IKIGAI::RENDER;


DriverVk* UtilityVk::GetDriver() {
	if (mDriver) {
		return mDriver;
	}
	return static_cast<DriverVk*>(DriverInterface::Get());
}

VmaBuffer UtilityVk::CreateBuffer(uint64_t size, vk::BufferUsageFlags usage, MemoryUsage memoryUsage)
{
	auto buffer_create_info = vk::BufferCreateInfo()
	                          .setSize(size)
	                          .setUsage(usage)
	                          .setSharingMode(vk::SharingMode::eExclusive);
	VkBufferCreateInfo vkBufferInfo = buffer_create_info;

	VmaAllocationCreateInfo allocInfo{};
	if (memoryUsage == MemoryUsage::Staging) {
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	} else {
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	}

	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	const VkResult result = vmaCreateBuffer(GetDriver()->mAllocator, &vkBufferInfo, &allocInfo, &buffer, &allocation, nullptr);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("vmaCreateBuffer failed");
	}

	return VmaBuffer(GetDriver()->mAllocator, buffer, allocation);
}

vk::raii::ImageView UtilityVk::CreateImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_flags, size_t layers, vk::ImageViewType type, uint32_t mip_levels ) {
	auto image_subresource_range = vk::ImageSubresourceRange()
	                               .setAspectMask(aspect_flags)
	                               .setLevelCount(mip_levels)
	                               .setLayerCount(1);

	auto image_view_create_info = vk::ImageViewCreateInfo()
	                              .setImage(image)
	                              .setViewType(vk::ImageViewType::e2D)
	                              .setFormat(format)
	                              .setSubresourceRange(image_subresource_range);

	return GetDriver()->mDevice.createImageView(image_view_create_info);
}

std::tuple<VmaImage, vk::raii::ImageView> UtilityVk::CreateImage(uint32_t width,
	uint32_t height, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect_flags, size_t layers, vk::ImageViewType type,
	uint32_t mip_levels)
{
	auto image_create_info = vk::ImageCreateInfo()
	                         .setImageType(vk::ImageType::e2D)
	                         .setFormat(format)
	                         .setExtent({width, height, 1})
	                         .setMipLevels(mip_levels)
	                         .setArrayLayers(1)
	                         .setSamples(vk::SampleCountFlagBits::e1)
	                         .setTiling(vk::ImageTiling::eOptimal)
	                         .setUsage(usage)
	                         .setSharingMode(vk::SharingMode::eExclusive)
	                         .setInitialLayout(vk::ImageLayout::eUndefined);
	VkImageCreateInfo vkImageInfo = image_create_info;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	const VkResult result = vmaCreateImage(GetDriver()->mAllocator, &vkImageInfo, &allocInfo, &image, &allocation, nullptr);
	if (result != VK_SUCCESS) {
		throw std::runtime_error("vmaCreateImage failed");
	}

	VmaImage vmaImage(GetDriver()->mAllocator, image, allocation);
	auto image_view = CreateImageView(*vmaImage, format, aspect_flags, layers, type, mip_levels);

	return {std::move(vmaImage), std::move(image_view)};
}

void UtilityVk::SetMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 src_stage, vk::PipelineStageFlags2 dst_stage) {
	auto memory_barrier = vk::MemoryBarrier2()
	                      .setSrcStageMask(src_stage)
	                      .setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
	                      .setDstStageMask(dst_stage)
	                      .setDstAccessMask(vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead);

	auto dependency_info = vk::DependencyInfo()
		.setMemoryBarriers(memory_barrier);

	cmdbuf.pipelineBarrier2(dependency_info);
}


void UtilityVk::SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::ImageAspectFlags aspect_mask,
	vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level, uint32_t level_count,
	uint32_t base_array_layer, uint32_t layer_count) {
	assert(new_layout != vk::ImageLayout::eUndefined && new_layout != vk::ImageLayout::ePreinitialized);

	constexpr vk::PipelineStageFlags depth_stage_mask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
		vk::PipelineStageFlagBits::eLateFragmentTests;

	constexpr vk::PipelineStageFlags sampled_stage_mask = vk::PipelineStageFlagBits::eVertexShader |
		vk::PipelineStageFlagBits::eFragmentShader;

	vk::PipelineStageFlags src_stage_mask = vk::PipelineStageFlagBits::eTopOfPipe;
	vk::PipelineStageFlags dst_stage_mask = vk::PipelineStageFlagBits::eBottomOfPipe;

	vk::AccessFlags src_access_mask;
	vk::AccessFlags dst_access_mask;

	switch (old_layout) {
	case vk::ImageLayout::eUndefined:
		break;

	case vk::ImageLayout::eGeneral:
		src_stage_mask = vk::PipelineStageFlagBits::eAllCommands;
		src_access_mask = vk::AccessFlagBits::eMemoryWrite;
		break;

	case vk::ImageLayout::eColorAttachmentOptimal:
		src_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		src_stage_mask = depth_stage_mask;
		src_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		src_stage_mask = depth_stage_mask | sampled_stage_mask;
		break;

	case vk::ImageLayout::eShaderReadOnlyOptimal:
		src_stage_mask = sampled_stage_mask;
		break;

	case vk::ImageLayout::eTransferSrcOptimal:
		src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		break;

	case vk::ImageLayout::eTransferDstOptimal:
		src_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		src_access_mask = vk::AccessFlagBits::eTransferWrite;
		break;

	case vk::ImageLayout::ePreinitialized:
		src_stage_mask = vk::PipelineStageFlagBits::eHost;
		src_access_mask = vk::AccessFlagBits::eHostWrite;
		break;

	case vk::ImageLayout::ePresentSrcKHR:
		break;

	default:
		assert(false);
		break;
	}

	switch (new_layout) {
	case vk::ImageLayout::eGeneral:
		dst_stage_mask = vk::PipelineStageFlagBits::eAllCommands;
		dst_access_mask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
		break;

	case vk::ImageLayout::eColorAttachmentOptimal:
		dst_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dst_access_mask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilAttachmentOptimal:
		dst_stage_mask = depth_stage_mask;
		dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		break;

	case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
		dst_stage_mask = depth_stage_mask | sampled_stage_mask;
		dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eShaderRead |
			vk::AccessFlagBits::eInputAttachmentRead;
		break;

	case vk::ImageLayout::eShaderReadOnlyOptimal:
		dst_stage_mask = sampled_stage_mask;
		dst_access_mask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eInputAttachmentRead;
		break;

	case vk::ImageLayout::eTransferSrcOptimal:
		dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		dst_access_mask = vk::AccessFlagBits::eTransferRead;
		break;

	case vk::ImageLayout::eTransferDstOptimal:
		dst_stage_mask = vk::PipelineStageFlagBits::eTransfer;
		dst_access_mask = vk::AccessFlagBits::eTransferWrite;
		break;

	case vk::ImageLayout::ePresentSrcKHR:
		// vkQueuePresentKHR performs automatic visibility operations
		break;

	default:
		assert(false);
		break;
	}

	auto subresource_range = vk::ImageSubresourceRange()
		.setAspectMask(aspect_mask)
		.setBaseMipLevel(base_mip_level)
		.setLevelCount(level_count)
		.setBaseArrayLayer(base_array_layer)
		.setLayerCount(layer_count);

	auto image_memory_barrier = vk::ImageMemoryBarrier()
		.setSrcAccessMask(src_access_mask)
		.setDstAccessMask(dst_access_mask)
		.setOldLayout(old_layout)
		.setNewLayout(new_layout)
		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setImage(image)
		.setSubresourceRange(subresource_range);

	cmdbuf.pipelineBarrier(src_stage_mask, dst_stage_mask, {}, {}, {}, {image_memory_barrier});
}

void UtilityVk::SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::Format format,
	vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level, uint32_t level_count,
	uint32_t base_array_layer, uint32_t layer_count) {
	vk::ImageAspectFlags aspect_mask;
	if (new_layout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
		aspect_mask = vk::ImageAspectFlagBits::eDepth;
		if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
			aspect_mask |= vk::ImageAspectFlagBits::eStencil;
		}
	} else {
		aspect_mask = vk::ImageAspectFlagBits::eColor;
	}
	SetImageMemoryBarrier(cmdbuf, image, aspect_mask, old_layout, new_layout, base_mip_level, level_count,
		base_array_layer, layer_count);
}

void UtilityVk::OneTimeSubmit(std::function<void(const vk::raii::CommandBuffer&)> func) {
	auto command_buffer_allocate_info = vk::CommandBufferAllocateInfo()
	                                    .setCommandBufferCount(1)
	                                    .setCommandPool(*mDriver->mCommandPool)
	                                    .setLevel(vk::CommandBufferLevel::ePrimary);

	auto command_buffers = mDriver->mDevice.allocateCommandBuffers(command_buffer_allocate_info);
	auto cmdbuf = std::move(command_buffers.at(0));

	auto command_buffer_begin_info = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

	cmdbuf.begin(command_buffer_begin_info);
	func(cmdbuf);
	cmdbuf.end();

	auto submit_info = vk::SubmitInfo()
		.setCommandBuffers(*cmdbuf);

	mDriver->mQueue.submit(submit_info);
	mDriver->mQueue.waitIdle();
}


#endif
