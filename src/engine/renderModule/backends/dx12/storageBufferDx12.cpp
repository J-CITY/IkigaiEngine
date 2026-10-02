#include "storageBufferDx12.h"
#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "driverDx12.h"
#include "d3dx12/DirectXHelpers.h"

namespace IKIGAI::RENDER {
	StorageBufferDx12::StorageBufferDx12(const void* data, size_t sz, size_t stride): StorageBufferInterface(sz, stride) {
		if (mSizeByte > 0) {
			init();
			if (data) {
				StorageBufferDx12::setData(data, sz, stride);
			}
		}
	}

	void StorageBufferDx12::init() {
		if (mSizeByte == 0) {
			return;
		}
		const D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(mSizeByte, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		mBuffer = d3dUtil::CreateResource(desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

		//TODO: Maybe should set dynamic 
		mState = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;// D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		//mState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		d3dUtil::OneTimeSubmit([&](ID3D12GraphicsCommandList* cmdlist) {
			DirectX::TransitionResource(cmdlist, mBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, mState);
		});

		// SRV
		{
			//D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
			//srvHeapDesc.NumDescriptors = 1;
			//srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			//srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			//d3dUtil::GetDriver()->getDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap));
			//mSrvHeapBegin = mSrvHeap->GetCPUDescriptorHandleForHeapStart();

			mSrvHeapBegin = d3dUtil::GetDriver()->getDescriptorHeapCPUHandle();
			mGpuSrvDescriptorHandle = d3dUtil::GetDriver()->getDescriptorHeapGPUHandle();
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = mSize;
			srvDesc.Buffer.StructureByteStride = mStride;
			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
			d3dUtil::GetDriver()->getDevice()->CreateShaderResourceView(mBuffer.Get(), &srvDesc, mSrvHeapBegin);
			d3dUtil::GetDriver()->getDescriptorHeapCPUHandle().Offset(1, d3dUtil::GetDriver()->getDescriptorIncSize());
			d3dUtil::GetDriver()->getDescriptorHeapGPUHandle().Offset(1, d3dUtil::GetDriver()->getDescriptorIncSize());

		}

		// UAV
		{
			//D3D12_DESCRIPTOR_HEAP_DESC uavheapDesc{};
			//uavheapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			//uavheapDesc.NumDescriptors = 1;
			//uavheapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			//d3dUtil::GetDriver()->getDevice()->CreateDescriptorHeap(&uavheapDesc, IID_PPV_ARGS(&mUavHeap));
			//mUavHeapBegin = mUavHeap->GetCPUDescriptorHandleForHeapStart();
			mUavHeapBegin = d3dUtil::GetDriver()->getDescriptorHeapCPUHandle();
			mGpuUavDescriptorHandle = d3dUtil::GetDriver()->getDescriptorHeapGPUHandle();

			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
			uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements = static_cast<UINT>(mSizeByte / sizeof(uint32_t));
			uavDesc.Buffer.StructureByteStride = 0;
			uavDesc.Buffer.CounterOffsetInBytes = 0;
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
			d3dUtil::GetDriver()->getDevice()->CreateUnorderedAccessView(mBuffer.Get(), nullptr, &uavDesc, mUavHeapBegin);
			d3dUtil::GetDriver()->getDescriptorHeapCPUHandle().Offset(1, d3dUtil::GetDriver()->getDescriptorIncSize());
			d3dUtil::GetDriver()->getDescriptorHeapGPUHandle().Offset(1, d3dUtil::GetDriver()->getDescriptorIncSize());
		}
	}

	StorageBufferDx12::~StorageBufferDx12() {
		if (mBuffer) {
			d3dUtil::GetDriver()->destroyDeferred(mBuffer);
		}
	}

	const Dx12Resource& StorageBufferDx12::getBuffer() const {
		return mBuffer;
	}

	void StorageBufferDx12::setSubData(const void* data, size_t sz, size_t offset) {
		if (!mBuffer || offset + sz > mSizeByte || !data) {
			return;
		}

		auto buffer = d3dUtil::CreateBuffer(sz, D3D12_HEAP_TYPE_UPLOAD);
		void* mapBuffer = nullptr;
		buffer->Map(0, nullptr, &mapBuffer);
		memcpy(mapBuffer, data, sz);
		buffer->Unmap(0, nullptr);

		auto barrier = DirectX::ScopedBarrier(d3dUtil::GetDriver()->getCommandList().Get(), {
			CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST)
		});

		d3dUtil::GetDriver()->getCommandList()->CopyBufferRegion(mBuffer.Get(), offset, buffer.Get(), 0, sz);
		d3dUtil::GetDriver()->destroyDeferred(buffer);

		DirectX::ScopedBarrier(d3dUtil::GetDriver()->getCommandList().Get(), {
			CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, mState)
		});
	}

	void StorageBufferDx12::setData(const void* data, size_t sz, size_t stride) {
		const size_t newSizeByte = sz * stride;
		if (newSizeByte > mSizeByte) {
			mSize = sz;
			mStride = stride;
			mSizeByte = newSizeByte;
			if (mBuffer) {
				d3dUtil::GetDriver()->destroyDeferred(mBuffer);
				mBuffer = nullptr;
			}
			if (mSizeByte > 0) {
				init();
			}
		}
		if (!mBuffer || !data || newSizeByte == 0) {
			return;
		}

		auto buffer = d3dUtil::CreateBuffer(newSizeByte, D3D12_HEAP_TYPE_UPLOAD);
		void* mapBuffer = nullptr;
		buffer->Map(0, nullptr, &mapBuffer);
		memcpy(mapBuffer, data, newSizeByte);
		buffer->Unmap(0, nullptr);

		auto barrier = DirectX::ScopedBarrier(d3dUtil::GetDriver()->getCommandList().Get(), {
			CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, D3D12_RESOURCE_STATE_COPY_DEST)
		});

		d3dUtil::GetDriver()->getCommandList()->CopyBufferRegion(mBuffer.Get(), 0, buffer.Get(), 0, newSizeByte);
		d3dUtil::GetDriver()->destroyDeferred(buffer);

		auto barrier2 = DirectX::ScopedBarrier(d3dUtil::GetDriver()->getCommandList().Get(), {
			CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, mState)
		});
	}

	void StorageBufferDx12::transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES state) {
		if (!mBuffer || !cmd || mState == state) {
			return;
		}
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mBuffer.Get(), mState, state);
		cmd->ResourceBarrier(1, &barrier);
		mState = state;
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE StorageBufferDx12::getSRVHandler() {
		return mGpuSrvDescriptorHandle;
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE StorageBufferDx12::getUAVHandler() {
		return mGpuUavDescriptorHandle;
	}
}
#endif
