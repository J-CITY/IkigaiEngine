#include "vmaVk.h"

#ifdef VULKAN_BACKEND

namespace IKIGAI::RENDER {

VmaBuffer::VmaBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation)
	: mAllocator(allocator), mBuffer(buffer), mAllocation(allocation) {
}

VmaBuffer::VmaBuffer(VmaBuffer&& other) noexcept
	: mAllocator(other.mAllocator), mBuffer(other.mBuffer), mAllocation(other.mAllocation) {
	other.mAllocator = nullptr;
	other.mBuffer = VK_NULL_HANDLE;
	other.mAllocation = nullptr;
}

VmaBuffer& VmaBuffer::operator=(VmaBuffer&& other) noexcept {
	if (this != &other) {
		reset();
		mAllocator = other.mAllocator;
		mBuffer = other.mBuffer;
		mAllocation = other.mAllocation;
		other.mAllocator = nullptr;
		other.mBuffer = VK_NULL_HANDLE;
		other.mAllocation = nullptr;
	}
	return *this;
}

VmaBuffer::~VmaBuffer() {
	reset();
}

void VmaBuffer::reset() {
	if (mBuffer && mAllocator) {
		vmaDestroyBuffer(mAllocator, mBuffer, mAllocation);
	}
	mBuffer = VK_NULL_HANDLE;
	mAllocation = nullptr;
	mAllocator = nullptr;
}

void VmaBuffer::upload(const void* data, size_t size, VkDeviceSize offset) {
	if (!mAllocator || !mAllocation || !data || size == 0) {
		return;
	}
	vmaCopyMemoryToAllocation(mAllocator, data, mAllocation, offset, size);
}

vk::Buffer VmaBuffer::get() const {
	return vk::Buffer(mBuffer);
}

vk::Buffer VmaBuffer::operator*() const {
	return get();
}

VmaBuffer::operator bool() const {
	return mBuffer != VK_NULL_HANDLE;
}

VmaImage::VmaImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation)
	: mAllocator(allocator), mImage(image), mAllocation(allocation) {
}

VmaImage::VmaImage(VmaImage&& other) noexcept
	: mAllocator(other.mAllocator), mImage(other.mImage), mAllocation(other.mAllocation) {
	other.mAllocator = nullptr;
	other.mImage = VK_NULL_HANDLE;
	other.mAllocation = nullptr;
}

VmaImage& VmaImage::operator=(VmaImage&& other) noexcept {
	if (this != &other) {
		reset();
		mAllocator = other.mAllocator;
		mImage = other.mImage;
		mAllocation = other.mAllocation;
		other.mAllocator = nullptr;
		other.mImage = VK_NULL_HANDLE;
		other.mAllocation = nullptr;
	}
	return *this;
}

VmaImage::~VmaImage() {
	reset();
}

void VmaImage::reset() {
	if (mImage && mAllocator) {
		vmaDestroyImage(mAllocator, mImage, mAllocation);
	}
	mImage = VK_NULL_HANDLE;
	mAllocation = nullptr;
	mAllocator = nullptr;
}

vk::Image VmaImage::get() const {
	return vk::Image(mImage);
}

vk::Image VmaImage::operator*() const {
	return get();
}

VmaImage::operator bool() const {
	return mImage != VK_NULL_HANDLE;
}

}

#endif
