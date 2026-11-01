#include "uploadRingDx12.h"
#ifdef DX12_BACKEND
#include <algorithm>

namespace IKIGAI::RENDER {
UploadRingDx12::~UploadRingDx12() {
	release();
}

UploadRingDx12::Page UploadRingDx12::CreatePage(uint64_t size) {
	Page page;
	page.Size = size;
	page.Buffer = d3dUtil::CreateBuffer(size, D3D12_HEAP_TYPE_UPLOAD);
	void* mapped = nullptr;
	const D3D12_RANGE noRead { 0, 0 };
	ThrowIfFailed(page.Buffer->Map(0, &noRead, &mapped));
	page.Cpu = static_cast<uint8_t*>(mapped);
	page.Gpu = page.Buffer->GetGPUVirtualAddress();
	return page;
}

UploadAllocationDx12 UploadRingDx12::allocate(uint64_t size, uint64_t alignment) {
	alignment = std::max<uint64_t>(alignment, 1);
	size = std::max<uint64_t>(size, 1);
	auto alignedOffset = [alignment](uint64_t offset) { return (offset + alignment - 1) / alignment * alignment; };
	if (mActivePages.empty() || alignedOffset(mActivePages.back().Used) + size > mActivePages.back().Size) {
		if (size > PageSize) {
			mActivePages.push_back(CreatePage(size));
		}
		else if (!mFreePages.empty()) {
			mActivePages.push_back(std::move(mFreePages.back()));
			mFreePages.pop_back();
		}
		else {
			mActivePages.push_back(CreatePage(PageSize));
		}
	}
	auto& page = mActivePages.back();
	const uint64_t offset = alignedOffset(page.Used);
	page.Used = offset + size;
	UploadAllocationDx12 allocation;
	allocation.Resource = page.Buffer.Get();
	allocation.Offset = offset;
	allocation.Cpu = page.Cpu + offset;
	allocation.Gpu = page.Gpu + offset;
	return allocation;
}

void UploadRingDx12::retireFrame(uint64_t fenceValue) {
	for (auto& page : mActivePages) {
		page.RetireFence = fenceValue;
		mRetiredPages.push_back(std::move(page));
	}
	mActivePages.clear();
}

void UploadRingDx12::beginFrame(uint64_t completedFenceValue) {
	++mSerial;
	std::vector<Page> pending;
	for (auto& page : mRetiredPages) {
		if (page.RetireFence > completedFenceValue) {
			pending.push_back(std::move(page));
			continue;
		}
		// The GPU has finished with this page; oversized and surplus pages are released immediately.
		if (page.Size == PageSize && mFreePages.size() < MaxFreePages) {
			page.Used = 0;
			mFreePages.push_back(std::move(page));
		}
	}
	mRetiredPages = std::move(pending);
}

uint64_t UploadRingDx12::serial() const {
	return mSerial;
}

void UploadRingDx12::release() {
	mActivePages.clear();
	mRetiredPages.clear();
	mFreePages.clear();
}
}
#endif
