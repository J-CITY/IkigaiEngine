#pragma once

#ifdef DX12_BACKEND
#include <memory>
#include "d3dUtil.h"
#include "../interface/indexBufferInterface.h"
#include <d3d12.h>

namespace IKIGAI::RENDER {
	class IndexBufferDx12 : public IndexBufferInterface, public std::enable_shared_from_this<IndexBufferDx12> {
		Dx12Resource mBuffer;
		D3D12_RESOURCE_STATES mState;

		IndexBufferDx12(void* data, size_t sz, size_t stride);
	public:
		template<class T>
		IndexBufferDx12(const std::vector<T>& vertices) : IndexBufferDx12((void*)vertices.data(), vertices.size(), sizeof(T)) {

		}
		~IndexBufferDx12() override;
		void bind() override;;
		void unbind() override;;
		void setData(const void* data, size_t sz, size_t stride) override;

		const Dx12Resource& getBuffer() const;
	};
}
#endif
