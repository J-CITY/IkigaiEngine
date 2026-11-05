#include "uniformBufferDx12.h"

#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "d3dx12/DirectXHelpers.h"
#include "driverDx12.h"
#include "uploadRingDx12.h"
#include <cstring>

IKIGAI::RENDER::UniformBufferDx12::UniformBufferDx12(const void* data, size_t sz)
	: UniformBufferInterface(DirectX::AlignUp((int)sz, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)) {
	mData.resize(mSizeByte);
	if (data) {
		UniformBufferDx12::setData(data, sz);
	}
}

IKIGAI::RENDER::UniformBufferDx12::~UniformBufferDx12() = default;

void IKIGAI::RENDER::UniformBufferDx12::setData(const void* data, size_t sz, size_t offset) {
	requireOwnerThread();
	if (!data || sz == 0) {
		return;
	}
	if (offset > SIZE_MAX - sz || offset + sz > INT_MAX - D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) {
		throw std::invalid_argument("Uniform buffer range is too large");
	}
	if (offset + sz > mSizeByte) {
		mSizeByte = DirectX::AlignUp((int)(offset + sz), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		mData.resize(mSizeByte);
	}
	std::memcpy(mData.data() + offset, data, sz);
	mUploadSerial = 0;
}

D3D12_GPU_VIRTUAL_ADDRESS IKIGAI::RENDER::UniformBufferDx12::getGpuAddress() {
	auto& ring = d3dUtil::GetDriver()->getUploadRing();
	if (mUploadSerial != ring.serial() || mGpuAddress == 0) {
		const auto allocation = ring.allocate(mSizeByte, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		if (!mData.empty()) {
			std::memcpy(allocation.Cpu, mData.data(), mData.size());
		}
		mGpuAddress = allocation.Gpu;
		mUploadSerial = ring.serial();
	}
	return mGpuAddress;
}
#endif
