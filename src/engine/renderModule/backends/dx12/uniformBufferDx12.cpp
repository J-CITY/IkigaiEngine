#include "uniformBufferDx12.h"

#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "driverDx12.h"
#include "d3dx12/d3dx12.h"
#include "d3dx12/DirectXHelpers.h"

IKIGAI::RENDER::UniformBufferDx12::UniformBufferDx12(const void* data, size_t sz) : UniformBufferInterface(DirectX::AlignUp((int)sz, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)) {
	mBuffer = d3dUtil::CreateBuffer(mSizeByte);

	mState = D3D12_RESOURCE_STATE_COMMON;
	d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
		DirectX::TransitionResource(cmdlist, mBuffer.Get(), mState, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	});
	mState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	if (data) {
		UniformBufferDx12::setData(data, sz);
	}
}

IKIGAI::RENDER::UniformBufferDx12::~UniformBufferDx12() {
	d3dUtil::GetDriver()->destroyDeferred(mBuffer);
}

void IKIGAI::RENDER::UniformBufferDx12::setData(const void* data, size_t sz, size_t offset) {
	(void)offset;
	if (!data || sz == 0 || !d3dUtil::GetDriver() || !d3dUtil::GetDriver()->getCommandList()) {
		return;
	}
	if (sz > mSizeByte) {
		mSizeByte = DirectX::AlignUp((int)sz, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		d3dUtil::GetDriver()->destroyDeferred(mBuffer);
		mBuffer = d3dUtil::CreateBuffer(mSizeByte);
		mState = D3D12_RESOURCE_STATE_COMMON;
	}

	auto buffer = d3dUtil::CreateBuffer(sz, D3D12_HEAP_TYPE_UPLOAD);
	if (!buffer || !mBuffer) {
		return;
	}
	void* mapBuffer = nullptr;
	buffer->Map(0, nullptr, &mapBuffer);
	memcpy(mapBuffer, data, sz);
	buffer->Unmap(0, nullptr);

	auto* cmd = d3dUtil::GetDriver()->getCommandList().Get();
	if (mState != D3D12_RESOURCE_STATE_COPY_DEST) {
		const auto toCopy = CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST);
		cmd->ResourceBarrier(1, &toCopy);
	}
	cmd->CopyBufferRegion(mBuffer.Get(), 0, buffer.Get(), 0, sz);
	const auto toUse = CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	cmd->ResourceBarrier(1, &toUse);
	mState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	d3dUtil::GetDriver()->destroyDeferred(buffer);
}

const IKIGAI::RENDER::Dx12Resource& IKIGAI::RENDER::UniformBufferDx12::getBuffer() const {
	return mBuffer;
}
#endif

