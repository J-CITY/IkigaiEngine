#pragma once
#ifdef DX12_BACKEND
#include "renderModule/backends/interface/uniformBufferInterface.h"
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <vector>

namespace IKIGAI::RENDER {
// Constant buffer backed by the driver's per-frame upload ring. The contents are uploaded when the
// buffer is bound after a change, so each draw keeps the data that was current when it was recorded.
// The CPU copy is uploaded again when the buffer is bound in a later frame without an update.
class UniformBufferDx12 : public UniformBufferInterface {
private:
	std::vector<std::byte> mData;
	D3D12_GPU_VIRTUAL_ADDRESS mGpuAddress = 0;
	// Upload ring serial of mGpuAddress; 0 when the CPU copy changed after the last upload.
	uint64_t mUploadSerial = 0;

public:
	UniformBufferDx12(const void* data, size_t sz);
	template <class T>
	UniformBufferDx12(const T& data)
		: UniformBufferDx12((void*)&data, sizeof(T)) { }
	~UniformBufferDx12() override;
	void setData(const void* data, size_t sz, size_t offset = 0) override;

	// Address of the current contents; valid until the frame being recorded completes on the GPU.
	D3D12_GPU_VIRTUAL_ADDRESS getGpuAddress();
};
}
#endif
