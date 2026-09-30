#pragma once

#ifdef VULKAN_BACKEND
#include "driverVk.h"
#include "shaderVk.h"
namespace IKIGAI::RENDER {
	class UniformBufferVk : public UniformBufferInterface {
	public:
		vk::raii::Buffer mBuffer = nullptr;
		vk::raii::DeviceMemory mDeviceMemory = nullptr;
	public:
		const vk::raii::Buffer& getBuffer() const { return mBuffer; }

		UniformBufferVk(void* data, size_t size);
		~UniformBufferVk() override;

		template<class T>
		UniformBufferVk(const T& data) : UniformBufferVk((void*)&data, sizeof(T)) {

		}

		void setData(const void* data, size_t sz, size_t offset = 0) override;
		
	};



}
#endif
