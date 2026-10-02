#include "vertexBufferDx12.h"
#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "driverDx12.h"
#include "d3dx12/d3dx12.h"
#include "d3dx12/DirectXHelpers.h"

IKIGAI::RENDER::VertexBufferDx12::VertexBufferDx12(void *data, size_t sz, size_t stride): VertexBufferInterface(sz, stride) {
	mBuffer = d3dUtil::CreateBuffer(mSizeByte);

	mState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

	d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList *cmdlist) {
		DirectX::TransitionResource(cmdlist, mBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, mState);
	});

	VertexBufferDx12::setData(data, sz, stride);
}

IKIGAI::RENDER::VertexBufferDx12::~VertexBufferDx12() {
	d3dUtil::GetDriver()->destroyDeferred(mBuffer);
}

const IKIGAI::RENDER::Dx12Resource& IKIGAI::RENDER::VertexBufferDx12::getBuffer() const {
	return mBuffer;
}

void IKIGAI::RENDER::VertexBufferDx12::bind() {
	d3dUtil::GetDriver()->setVertexBuffer(shared_from_this());
}

void IKIGAI::RENDER::VertexBufferDx12::unbind() {
	d3dUtil::GetDriver()->setVertexBuffer(nullptr);
}

void IKIGAI::RENDER::VertexBufferDx12::setData(const void* data, size_t sz, size_t stride) {
	if (sz * stride > mSizeByte) {
		mSize = sz;
		mStride = stride;
		mSizeByte = mSize * mStride;
		d3dUtil::GetDriver()->destroyDeferred(mBuffer);
		mBuffer = d3dUtil::CreateBuffer(mSizeByte);
	}

	auto buffer = d3dUtil::CreateBuffer(sz * stride, D3D12_HEAP_TYPE_UPLOAD);
	void* mapBuffer = nullptr;
	buffer->Map(0, nullptr, &mapBuffer);
	memcpy(mapBuffer, data, sz * stride);
	buffer->Unmap(0, nullptr);

	auto barrier = DirectX::ScopedBarrier(d3dUtil::GetDriver()->getCommandList().Get(), {
		CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST)
	});
	d3dUtil::GetDriver()->getCommandList()->CopyBufferRegion(mBuffer.Get(), 0, buffer.Get(), 0, sz * stride);
	d3dUtil::GetDriver()->destroyDeferred(buffer);
}
#endif
