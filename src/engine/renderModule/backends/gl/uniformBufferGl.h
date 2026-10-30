#pragma once
#include "gpuMemoryGl.h"

#ifdef OPENGL_BACKEND
#include "../interface/uniformBufferInterface.h"
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
class UniformBufferGl : public UniformBufferInterface {
public:
	using Id = unsigned;

private:
	Id mId = 0;
	std::vector<std::byte> mLastUpload = {};
#if IKIGAI_ENABLE_MEMORY_TRACKING
	PROFILER::GpuAllocationToken mMemory;
#endif
public:
	UniformBufferGl(const void* data, size_t sz);
	UniformBufferGl(size_t sz);
	template <class T>
	UniformBufferGl(const T& data)
		: UniformBufferGl(static_cast<const void*>(&data), sizeof(T)) { }
	~UniformBufferGl() override;
	void setData(const void* data, size_t sz, size_t offset = 0) override;

	void bind();
	void bindToShader(unsigned slot);
	void unbind();
	unsigned getId() const;
};
}
#endif
