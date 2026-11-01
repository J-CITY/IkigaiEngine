#include "indexBufferDx12.h"
#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "driverDx12.h"
#include "uploadRingDx12.h"
#include "d3dx12/d3dx12.h"
#include "d3dx12/DirectXHelpers.h"

IKIGAI::RENDER::IndexBufferDx12::IndexBufferDx12(void* data, size_t sz, size_t stride): IndexBufferInterface(sz, stride) {

	mBuffer = d3dUtil::CreateBuffer(mSizeByte);

	SetGpuMemoryKindDx12(mBuffer.Get(), PROFILER::GpuResourceKind::IndexBuffer);
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
    transition(d3dUtil::GetDriver()->getCommandList().Get(),D3D12_RESOURCE_STATE_INDEX_BUFFER);
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
		SetGpuMemoryKindDx12(mBuffer.Get(), PROFILER::GpuResourceKind::IndexBuffer);
	}

	const auto staging = d3dUtil::GetDriver()->getUploadRing().allocate(sz * stride, D3D12_STANDARD_MAXIMUM_ELEMENT_ALIGNMENT_BYTE_MULTIPLE);
	memcpy(staging.Cpu, data, sz * stride);

	auto barrier = DirectX::ScopedBarrier(d3dUtil::GetDriver()->getCommandList().Get(), {
		CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST)
	});
	d3dUtil::GetDriver()->getCommandList()->CopyBufferRegion(mBuffer.Get(), 0, staging.Resource, staging.Offset, sz * stride);
}

const IKIGAI::RENDER::Dx12Resource& IKIGAI::RENDER::IndexBufferDx12::getBuffer() const {
	return mBuffer;
}
void IKIGAI::RENDER::IndexBufferDx12::transition(ID3D12GraphicsCommandList* cmd,D3D12_RESOURCE_STATES state) {
    if(state!=mState){auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(),mState,state);cmd->ResourceBarrier(1,&barrier);mState=state;}
}
#endif
