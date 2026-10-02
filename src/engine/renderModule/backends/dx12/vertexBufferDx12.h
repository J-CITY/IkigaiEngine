#pragma once
#ifdef DX12_BACKEND
#include <memory>
#include <d3d12.h>
#include <vector>
#include "d3dUtil.h"
#include "../interface/vertexBufferInterface.h"

namespace IKIGAI::RENDER {
	class VertexBufferDx12 : public VertexBufferInterface, public std::enable_shared_from_this<VertexBufferDx12> {
	private:
		Dx12Resource mBuffer;
		D3D12_RESOURCE_STATES mState;
	
		VertexBufferDx12(void *data, size_t sz, size_t stride);
	public:
		template <class T>
		VertexBufferDx12(const std::vector<T> &vertices): VertexBufferDx12((void*)vertices.data(), vertices.size(), sizeof(T)) {}
		~VertexBufferDx12() override;
		void bind() override;
		void unbind() override;
		void setData(const void* data, size_t sz, size_t stride) override;

		const Dx12Resource& getBuffer() const;
	};
}
#endif
