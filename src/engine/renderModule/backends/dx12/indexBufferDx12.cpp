#include "indexBufferDx12.h"
#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "driverDx12.h"
#include "d3dx12/d3dx12.h"
#include "d3dx12/DirectXHelpers.h"

IKIGAI::RENDER::IndexBufferDx12::IndexBufferDx12(void* data, size_t sz, size_t stride): IndexBufferInterface(sz, stride) {

	mBuffer = d3dUtil::CreateBuffer(mSizeByte);
	mState = D3D12_RESOURCE_STATE_COMMON;
	d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
		DirectX::TransitionResource(cmdlist, mBuffer.Get(), mState,
			D3D12_RESOURCE_STATE_INDEX_BUFFER);
	});
	mState = D3D12_RESOURCE_STATE_INDEX_BUFFER;
	IndexBufferDx12::setData(data, sz, stride);
}

IKIGAI::RENDER::IndexBufferDx12::~IndexBufferDx12() {
	d3dUtil::GetDriver()->destroyDeferred(mBuffer);
}

void IKIGAI::RENDER::IndexBufferDx12::bind() {
	d3dUtil::GetDriver()->setIndexBuffer(shared_from_this());
}

void IKIGAI::RENDER::IndexBufferDx12::unbind() {
	d3dUtil::GetDriver()->setIndexBuffer(nullptr);
}

void IKIGAI::RENDER::IndexBufferDx12::setData(const void* data, size_t sz, size_t stride) {
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

const IKIGAI::RENDER::Dx12Resource& IKIGAI::RENDER::IndexBufferDx12::getBuffer() const {
	return mBuffer;
}
#endif
