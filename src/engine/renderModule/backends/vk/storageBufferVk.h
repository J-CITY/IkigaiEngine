#pragma once

#ifdef VULKAN_BACKEND
#include <vector>
#include "helpers.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
namespace IKIGAI::RENDER {
	class StorageBufferVk : public StorageBufferInterface {
		VmaBuffer mBuffer;
	public:
		StorageBufferVk(void* data, size_t size, size_t stride);
		~StorageBufferVk() override;

		template<class T>
		StorageBufferVk(const std::vector<T>& vertices) : StorageBufferVk((void*)vertices.data(), vertices.size(), sizeof(T)) {

		}

		void setData(const void* data, size_t sz, size_t stride) override;
		void setSubData(const void* data, size_t sz, size_t offset) override;

		void bind() override;

		void unbind() override;

		const VmaBuffer& getBuffer() const { return mBuffer; }
	};
}
#endif
