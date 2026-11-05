#include "vmaVk.h"
#include <type_traits>

#ifdef VULKAN_BACKEND

namespace IKIGAI::RENDER {

#if IKIGAI_ENABLE_MEMORY_TRACKING
namespace {
template<class Handle> uint64_t HandleValue(Handle handle) {
    if constexpr (std::is_pointer_v<Handle>) return reinterpret_cast<uintptr_t>(handle);
    else return static_cast<uint64_t>(handle);
}
template<class Handle>
PROFILER::GpuAllocationInfo MemoryInfo(VmaAllocator allocator, VmaAllocation allocation, Handle handle,
    PROFILER::GpuResourceKind kind, uint64_t requestedBytes = 0) {
    VmaAllocationInfo allocationInfo{};
    vmaGetAllocationInfo(allocator, allocation, &allocationInfo);
    VkMemoryPropertyFlags flags{};
    vmaGetAllocationMemoryProperties(allocator, allocation, &flags);
    const VkPhysicalDeviceMemoryProperties* properties = nullptr;
    vmaGetMemoryProperties(allocator, &properties);
    PROFILER::GpuAllocationInfo result;
    result.backend = PROFILER::GpuBackend::Vulkan;
    result.device = reinterpret_cast<uintptr_t>(allocator);
    result.handle = HandleValue(handle); result.kind = kind;
    result.bytes = allocationInfo.size; result.requestedBytes = requestedBytes;
    result.heap = properties->memoryTypes[allocationInfo.memoryType].heapIndex;
    result.deviceLocal = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
    result.hostVisible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    return result;
}
}
#endif

VmaBuffer::VmaBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation,
    PROFILER::GpuResourceKind kind, uint64_t requestedBytes)
	: mAllocator(allocator), mBuffer(buffer), mAllocation(allocation) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    if (allocator && allocation) mMemory.track(MemoryInfo(allocator, allocation, buffer, kind, requestedBytes));
#else
    (void)kind; (void)requestedBytes;
#endif
}

VmaBuffer::VmaBuffer(VmaBuffer&& other) noexcept
	: mAllocator(other.mAllocator), mBuffer(other.mBuffer), mAllocation(other.mAllocation) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory = std::move(other.mMemory);
#endif
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
#if IKIGAI_ENABLE_MEMORY_TRACKING
        mMemory = std::move(other.mMemory);
#endif
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
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory.reset();
#endif
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
#if IKIGAI_ENABLE_MEMORY_TRACKING
    if (allocator && allocation) mMemory.track(MemoryInfo(allocator, allocation, image, PROFILER::GpuResourceKind::Texture));
#endif
}

VmaImage::VmaImage(VmaAllocator allocator, VkImage image, std::shared_ptr<VmaImage> backing)
	: mAllocator(allocator)
	, mImage(image)
	, mBacking(std::move(backing)) { }

VmaImage::VmaImage(VmaImage&& other) noexcept
	: mAllocator(other.mAllocator)
	, mImage(other.mImage)
	, mAllocation(other.mAllocation)
	, mBacking(std::move(other.mBacking)) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory = std::move(other.mMemory);
#endif
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
		mBacking = std::move(other.mBacking);
#if IKIGAI_ENABLE_MEMORY_TRACKING
        mMemory = std::move(other.mMemory);
#endif
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
	if (mImage && mAllocator && (!mBacking || mBacking->handle() != mImage)) {
		vmaDestroyImage(mAllocator, mImage, mAllocation);
	}
	mBacking.reset();
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory.reset();
#endif
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

void VmaBuffer::markPendingRelease() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory.markPending();
#endif
}
void VmaImage::markPendingRelease() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory.markPending();
#endif
}
void VmaBuffer::setMemoryName(const char* name) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory.setName(name);
#else
    (void)name;
#endif
}
void VmaImage::setMemoryName(const char* name) {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemory.setName(name);
#else
    (void)name;
#endif
}

}

#endif
