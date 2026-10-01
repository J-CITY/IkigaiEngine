#pragma once

#ifdef VULKAN_BACKEND
#include <vector>
#include <functional>
#include <tuple>
#include <vulkan/vulkan_raii.hpp>
#include "vmaVk.h"

namespace IKIGAI::RENDER {
	class DriverVk;

	struct UtilityVk {
		enum class MemoryUsage {
			GpuOnly,
			Staging
		};

		inline static DriverVk* mDriver = nullptr;
		static DriverVk* GetDriver();
		static VmaBuffer CreateBuffer(uint64_t size, vk::BufferUsageFlags usage, MemoryUsage memoryUsage = MemoryUsage::GpuOnly);
		static vk::raii::ImageView CreateImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspect_flags, size_t layers, vk::ImageViewType type, uint32_t mip_levels = 1);
		static std::tuple<VmaImage, vk::raii::ImageView> CreateImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect_flags, size_t layers, vk::ImageViewType type, uint32_t mip_levels = 1);

		static void SetMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::PipelineStageFlags2 src_stage, vk::PipelineStageFlags2 dst_stage);
		static void SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image,
		                           vk::ImageAspectFlags aspect_mask,
		                           vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level=0,
		                           uint32_t level_count = VK_REMAINING_MIP_LEVELS, uint32_t base_array_layer=0, uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS);
		static void SetImageMemoryBarrier(const vk::raii::CommandBuffer& cmdbuf, vk::Image image, vk::Format format,
		                           vk::ImageLayout old_layout, vk::ImageLayout new_layout, uint32_t base_mip_level=0,
		                           uint32_t level_count = VK_REMAINING_MIP_LEVELS, uint32_t base_array_layer=0, uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS);


		static void OneTimeSubmit(std::function<void(const vk::raii::CommandBuffer&)> func);

		//static vk::raii::Sampler CreateSamplerState(const SamplerStateVK& sampler_state) {
		//	static const std::unordered_map<Sampler, vk::Filter> FilterMap = {
		//		{Sampler::Linear, vk::Filter::eLinear},
		//		{Sampler::Nearest, vk::Filter::eNearest},
		//	};
		//
		//	static const std::unordered_map<TextureAddress, vk::SamplerAddressMode> AddressModeMap = {
		//		{TextureAddress::Clamp, vk::SamplerAddressMode::eClampToEdge},
		//		{TextureAddress::Wrap, vk::SamplerAddressMode::eRepeat},
		//		{TextureAddress::MirrorWrap, vk::SamplerAddressMode::eMirrorClampToEdge},
		//	};
		//
		//	auto sampler_create_info = vk::SamplerCreateInfo()
		//		.setMagFilter(FilterMap.at(sampler_state.sampler))
		//		.setMinFilter(FilterMap.at(sampler_state.sampler))
		//		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		//		.setAddressModeU(AddressModeMap.at(sampler_state.texture_address))
		//		.setAddressModeV(AddressModeMap.at(sampler_state.texture_address))
		//		.setAddressModeW(AddressModeMap.at(sampler_state.texture_address))
		//		.setMinLod(-1000)
		//		.setMaxLod(1000)
		//		.setMaxAnisotropy(1.0f);
		//
		//	return gContext->device.createSampler(sampler_create_info);
		//}
	};
}
#endif
