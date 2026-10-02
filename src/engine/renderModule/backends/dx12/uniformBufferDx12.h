#pragma once
#ifdef DX12_BACKEND
#include <d3d12.h>
#include "d3dUtil.h"
#include "renderModule/backends/interface/uniformBufferInterface.h"

namespace IKIGAI::RENDER {
	class UniformBufferDx12 : public UniformBufferInterface {
	private:
		Dx12Resource mBuffer;
		D3D12_RESOURCE_STATES mState;
	public:
		UniformBufferDx12(const void* data, size_t sz);
		template <class T>
		UniformBufferDx12(const T& data) : UniformBufferDx12((void*)&data, sizeof(T)) {}
		~UniformBufferDx12() override;
		void setData(const void* data, size_t sz, size_t offset = 0) override;

		const Dx12Resource& getBuffer() const;
	};
}
#endif
