#pragma once
#ifdef VULKAN_BACKEND
#include <vector>
#include "vmaVk.h"
#include "../interface/indexBufferInterface.h"
namespace IKIGAI::RENDER {
	class ShaderInterface;

	class IndexBufferVk : public IndexBufferInterface {
		VmaBuffer mBuffer;
	public:
		IndexBufferVk(void* data, size_t size, size_t stride);
		~IndexBufferVk() override;

		template<class T>
		IndexBufferVk(const std::vector<T>& vertices) : IndexBufferVk((void*)vertices.data(), vertices.size(), sizeof(T)) {

		}

		void setData(const void* data, size_t sz, size_t stride) override;

		void bind() override;

		void unbind() override;

		VmaBuffer& getBuffer() {
			return mBuffer;
		}
	};
}
#endif
