#pragma once
#include "d3dx12/d3dx12.h"

#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include <d3d12.h>
#include <vector>

namespace IKIGAI::RENDER {
	class StorageBufferDx12 : public StorageBufferInterface {
		D3D12_CPU_DESCRIPTOR_HANDLE mSrvHeapBegin{};
		D3D12_CPU_DESCRIPTOR_HANDLE mUavHeapBegin{};

		CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuSrvDescriptorHandle;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mGpuUavDescriptorHandle;
		Dx12Resource mBuffer;
		D3D12_RESOURCE_STATES mState;
		void init();
	public:
		StorageBufferDx12(const void* data, size_t sz, size_t stride);
		template <class T>
		StorageBufferDx12(const std::vector<T>& vertices) : StorageBufferDx12((void*)vertices.data(), vertices.size(), sizeof(T)) {}
		~StorageBufferDx12() override;
		void bind() override {};
		void unbind() override {};
		void setData(const void* data, size_t sz, size_t stride) override;
		void setSubData(const void* data, size_t sz, size_t offset) override;
		void transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES state);

		CD3DX12_GPU_DESCRIPTOR_HANDLE getSRVHandler();
		CD3DX12_GPU_DESCRIPTOR_HANDLE getUAVHandler();
		const Dx12Resource& getBuffer() const;
	};

}
#endif
