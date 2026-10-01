#pragma once

#ifdef VULKAN_BACKEND

#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#ifndef VMA_VULKAN_VERSION
#define VMA_VULKAN_VERSION 1003000
#endif

#include <cstddef>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

namespace IKIGAI::RENDER {
	class VmaBuffer {
	public:
		VmaBuffer() = default;
		VmaBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation);
		VmaBuffer(const VmaBuffer&) = delete;
		VmaBuffer& operator=(const VmaBuffer&) = delete;
		VmaBuffer(VmaBuffer&& other) noexcept;
		VmaBuffer& operator=(VmaBuffer&& other) noexcept;
		~VmaBuffer();

		void reset();
		void upload(const void* data, size_t size, VkDeviceSize offset = 0);

		vk::Buffer get() const;
		vk::Buffer operator*() const;
		explicit operator bool() const;

		VkBuffer handle() const { return mBuffer; }
		VmaAllocation allocation() const { return mAllocation; }
		VmaAllocator allocator() const { return mAllocator; }

	private:
		VmaAllocator mAllocator = nullptr;
		VkBuffer mBuffer = VK_NULL_HANDLE;
		VmaAllocation mAllocation = nullptr;
	};

	class VmaImage {
	public:
		VmaImage() = default;
		VmaImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation);
		VmaImage(const VmaImage&) = delete;
		VmaImage& operator=(const VmaImage&) = delete;
		VmaImage(VmaImage&& other) noexcept;
		VmaImage& operator=(VmaImage&& other) noexcept;
		~VmaImage();

		void reset();

		vk::Image get() const;
		vk::Image operator*() const;
		explicit operator bool() const;

		VkImage handle() const { return mImage; }
		VmaAllocation allocation() const { return mAllocation; }
		VmaAllocator allocator() const { return mAllocator; }

	private:
		VmaAllocator mAllocator = nullptr;
		VkImage mImage = VK_NULL_HANDLE;
		VmaAllocation mAllocation = nullptr;
	};
}

#endif
