#pragma once
#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include <cstdint>
#include <vector>

namespace IKIGAI::RENDER {
struct UploadAllocationDx12 {
	ID3D12Resource* Resource = nullptr;
	uint64_t Offset = 0;
	uint8_t* Cpu = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS Gpu = 0;
};

// Linear allocator for transient CPU-to-GPU data: constant buffer snapshots and staging copies.
// Pages stay persistently mapped. All pages used while a frame is recorded are retired with a fence
// value at the end of that frame and reused only after the GPU has passed it, so an allocation
// stays valid until the frame that recorded it has completed on the GPU.
class UploadRingDx12 {
public:
	static constexpr uint64_t PageSize = 4ull * 1024ull * 1024ull;
	static constexpr size_t MaxFreePages = 8;

	UploadRingDx12() = default;
	~UploadRingDx12();
	UploadRingDx12(const UploadRingDx12&) = delete;
	UploadRingDx12& operator=(const UploadRingDx12&) = delete;

	// Requests larger than PageSize get a dedicated page that is released after its frame completes.
	UploadAllocationDx12 allocate(uint64_t size, uint64_t alignment);
	// Called after the frame's command list was submitted; fenceValue is signalled after that submission.
	void retireFrame(uint64_t fenceValue);
	// Called when recording of a new frame begins; recycles pages the GPU has finished with.
	void beginFrame(uint64_t completedFenceValue);
	// Incremented by beginFrame. Allocations from an earlier serial may already be recycled.
	uint64_t serial() const;
	void release();

private:
	struct Page {
		Dx12Resource Buffer;
		uint8_t* Cpu = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS Gpu = 0;
		uint64_t Size = 0;
		uint64_t Used = 0;
		uint64_t RetireFence = 0;
	};
	static Page CreatePage(uint64_t size);

	std::vector<Page> mActivePages;
	std::vector<Page> mRetiredPages;
	std::vector<Page> mFreePages;
	uint64_t mSerial = 1;
};
}
#endif
