#ifdef VULKAN_BACKEND
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1003000
#include <volk.h>
#include <vk_mem_alloc.h>
#endif
