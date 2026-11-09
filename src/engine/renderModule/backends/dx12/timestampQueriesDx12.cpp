#include "driverDx12.h"

#ifdef DX12_BACKEND
#include "d3dUtil.h"
#include "deviceHealthDx12.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace IKIGAI::RENDER {
namespace {
	constexpr uint32_t TimestampQueryCount = 4096;
}

void DriverDx12::createTimestampQueries() {
	UINT64 frequency = 0;
	if (FAILED(mCommandQueue->GetTimestampFrequency(&frequency)) || frequency == 0) {
		std::cout << "DX12 timestamp queries unavailable on the direct queue\n";
		return;
	}
	D3D12_QUERY_HEAP_DESC heapDesc {};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	heapDesc.Count = TimestampQueryCount;
	ThrowIfFailed(mDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(mTimestampHeap.GetAddressOf())));
	// Committed (not D3D12MA) so the allocator shutdown statistics stay unchanged.
	const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(UINT64 { TimestampQueryCount } * sizeof(uint64_t));
	ThrowIfFailed(mDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(mTimestampReadback.GetAddressOf())));
	ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mTimestampFence.GetAddressOf())));
	mTimestampHeap->SetName(L"GpuStageProfiler timestamps");
	mTimestampReadback->SetName(L"GpuStageProfiler timestamp readback");
	mTimestampCapacity = TimestampQueryCount;
	mTimestampPeriodNanoseconds = 1e9 / double(frequency);
	mTimestampResolveSerial.assign(TimestampQueryCount, 0);
}

uint32_t DriverDx12::timestampQueryCapacity() const {
	return mTimestampCapacity;
}

void DriverDx12::writeTimestamp(uint32_t index) {
	if (index >= mTimestampCapacity) {
		throw UnsupportedGpuFeature("Timestamp query index out of range");
	}
	mCommandList->EndQuery(mTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
	mTimestampsWritten.push_back(index);
}

bool DriverDx12::resolveTimestampQueries() {
	if (mTimestampsWritten.empty()) {
		return false;
	}
	std::sort(mTimestampsWritten.begin(), mTimestampsWritten.end());
	mTimestampsWritten.erase(std::unique(mTimestampsWritten.begin(), mTimestampsWritten.end()), mTimestampsWritten.end());
	const UINT64 serial = mTimestampSerial + 1;
	size_t begin = 0;
	while (begin < mTimestampsWritten.size()) {
		size_t end = begin + 1;
		while (end < mTimestampsWritten.size() && mTimestampsWritten[end] == mTimestampsWritten[end - 1] + 1) {
			++end;
		}
		const uint32_t first = mTimestampsWritten[begin];
		const auto count = static_cast<UINT>(end - begin);
		mCommandList->ResolveQueryData(
			mTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, count, mTimestampReadback.Get(), UINT64 { first } * sizeof(uint64_t));
		for (size_t i = begin; i < end; ++i) {
			mTimestampResolveSerial[mTimestampsWritten[i]] = serial;
		}
		begin = end;
	}
	mTimestampsWritten.clear();
	return true;
}

void DriverDx12::signalTimestampQueries() {
	mDeviceHealth->check(mCommandQueue->Signal(mTimestampFence.Get(), ++mTimestampSerial), "Signal timestamp fence");
}

bool DriverDx12::readTimestamps(uint32_t first, uint32_t count, std::vector<uint64_t>& ticks) {
	if (count == 0 || first >= mTimestampCapacity || count > mTimestampCapacity - first) {
		return false;
	}
	UINT64 required = 0;
	for (uint32_t i = first; i < first + count; ++i) {
		if (mTimestampResolveSerial[i] == 0) {
			return false;
		}
		required = std::max(required, mTimestampResolveSerial[i]);
	}
	if (mDeviceHealth->completedValue(mTimestampFence.Get(), "Read timestamp fence") < required) {
		return false;
	}
	const D3D12_RANGE range { SIZE_T { first } * sizeof(uint64_t), SIZE_T { first + count } * sizeof(uint64_t) };
	void* mapped = nullptr;
	ThrowIfFailed(mTimestampReadback->Map(0, &range, &mapped));
	ticks.resize(count);
	std::memcpy(ticks.data(), static_cast<const uint8_t*>(mapped) + range.Begin, count * sizeof(uint64_t));
	const D3D12_RANGE written { 0, 0 };
	mTimestampReadback->Unmap(0, &written);
	// Consumed: the indices may be written again; stale values are never returned twice.
	std::fill(mTimestampResolveSerial.begin() + first, mTimestampResolveSerial.begin() + first + count, UINT64 { 0 });
	return true;
}
}
#endif
