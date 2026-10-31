#include "driverDx12.h"
#ifdef DX12_BACKEND
#include "storageBufferDx12.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace IKIGAI::RENDER {
namespace {
	StorageBufferDx12& NativeBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, const char* operation) {
		auto* native = dynamic_cast<StorageBufferDx12*>(buffer.get());
		if (!native || !native->getBuffer()) {
			throw std::invalid_argument(std::string(operation) + ": missing or empty DX12 storage buffer");
		}
		return *native;
	}

	void CheckRange(const StorageBufferInterface& buffer, size_t offset, size_t size, const char* operation) {
		if (offset > buffer.getSizeByte() || size > buffer.getSizeByte() - offset) {
			throw std::invalid_argument(std::string(operation) + ": range exceeds buffer size");
		}
	}

	void CheckUsage(const StorageBufferDx12& buffer, BufferUsage usage, const char* operation) {
		if (!HasBufferUsage(buffer.getUsage(), usage)) {
			throw std::invalid_argument(std::string(operation) + ": buffer was created without the required BufferUsage flag");
		}
	}

	void CheckTransferSource(const StorageBufferDx12& buffer, const char* operation) {
		if (!HasBufferUsage(buffer.getUsage(), BufferUsage::TRANSFER_SRC) && !HasBufferUsage(buffer.getUsage(), BufferUsage::READBACK)) {
			throw std::invalid_argument(std::string(operation) + ": buffer needs BufferUsage::TRANSFER_SRC or READBACK");
		}
	}

	void CheckIndirectArguments(const StorageBufferDx12& buffer, size_t offset, uint32_t drawCount, uint32_t stride, const char* operation) {
		CheckUsage(buffer, BufferUsage::INDIRECT, operation);
		if (offset % 4 != 0 || stride % 4 != 0 || stride < sizeof(DrawIndexedIndirectCommand)) {
			throw std::invalid_argument(std::string(operation) + ": offset and stride must be multiples of 4 and stride >= 20");
		}
		const uint64_t end = uint64_t(offset) + uint64_t(drawCount - 1) * stride + sizeof(DrawIndexedIndirectCommand);
		if (end > buffer.getSizeByte()) {
			throw std::invalid_argument(std::string(operation) + ": indirect arguments exceed buffer size");
		}
	}

	void LogGpuFeaturesDx12(const GpuFeatures& features) {
		std::cout << "GPU_FEATURES backend=dx12 compute=" << features.Compute << " usageFlags=" << features.BufferUsageFlags
				  << " transfer=" << features.BufferTransfer << " readback=" << features.BufferReadback << " drawIndirect=" << features.DrawIndirect
				  << " drawIndirectCount=" << features.DrawIndirectCount << " dispatchIndirect=" << features.DispatchIndirect
				  << " subgroupBasic=" << features.SubgroupBasic << " subgroupBallot=" << features.SubgroupBallot
				  << " subgroupSize=" << features.SubgroupSize << " int64BufferAtomics=" << features.BufferInt64Atomics
				  << " maxDrawIndirectCount=" << features.MaxDrawIndirectCount << " maxWorkGroups=" << features.MaxComputeWorkGroupCountX << "x"
				  << features.MaxComputeWorkGroupCountY << "x" << features.MaxComputeWorkGroupCountZ << std::endl;
	}
}

void DriverDx12::configureGpuFeatures() {
	GpuFeatures result;
	// ExecuteIndirect (including the count buffer), UAV buffers and copy queues are core D3D12.
	result.Compute = true;
	result.BufferUsageFlags = true;
	result.BufferTransfer = true;
	result.BufferReadback = true;
	result.DrawIndirect = true;
	result.DrawIndirectCount = true;
	result.DispatchIndirect = true;
	result.MaxDrawIndirectCount = std::numeric_limits<uint32_t>::max();
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 {};
	if (SUCCEEDED(mDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))) && options1.WaveOps) {
		// GLSL subgroup ops reach HLSL SM 6.0 wave intrinsics through SPIRV-Cross.
		result.SubgroupBasic = true;
		result.SubgroupBallot = true;
		// The runtime lane count lies in [min, max]; report the guaranteed minimum.
		result.SubgroupSize = options1.WaveLaneCountMin;
	}
	// Bound shaders are cross-compiled with SPIRV-Cross shader_model 60, which cannot emit
	// 64-bit atomics (SM 6.6). Reported false until the bound toolchain targets SM 6.6.
	result.BufferInt64Atomics = false;
	result.MaxComputeWorkGroupCountX = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
	result.MaxComputeWorkGroupCountY = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
	result.MaxComputeWorkGroupCountZ = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
	// Owned by agent C (textures / samplers).
	result.StorageImageMipViews = false;
	result.HalfFloatTextures = false;
	result.SamplerMinMaxReduction = false;
	// Timestamp fields are filled in gpuFeatures() once the query pool exists.
	result.TimestampQueries = false;
	result.TimestampPeriodNanoseconds = 0.0;
	mGpuFeatures = result;

	D3D12_INDIRECT_ARGUMENT_DESC dispatchArgument {};
	dispatchArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	D3D12_COMMAND_SIGNATURE_DESC dispatchDesc {};
	dispatchDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
	dispatchDesc.NumArgumentDescs = 1;
	dispatchDesc.pArgumentDescs = &dispatchArgument;
	ThrowIfFailed(mDevice->CreateCommandSignature(&dispatchDesc, nullptr, IID_PPV_ARGS(&mDispatchSignature)));
	LogGpuFeaturesDx12(mGpuFeatures);
}

GpuFeatures DriverDx12::gpuFeatures() const {
	auto features = mGpuFeatures;
	fillTextureGpuFeatures(features);
	// Timestamp queries are created after configureGpuFeatures() (timestampQueries*.cpp).
	features.TimestampQueries = mTimestampCapacity > 0;
	features.TimestampPeriodNanoseconds = mTimestampPeriodNanoseconds;
	return features;
}

ID3D12CommandSignature* DriverDx12::drawIndexedSignature(uint32_t stride) {
	auto& signature = mDrawIndexedSignatures[stride];
	if (!signature) {
		D3D12_INDIRECT_ARGUMENT_DESC argument {};
		argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
		D3D12_COMMAND_SIGNATURE_DESC desc {};
		desc.ByteStride = stride;
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs = &argument;
		ThrowIfFailed(mDevice->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&signature)));
	}
	return signature.Get();
}

std::shared_ptr<StorageBufferInterface> DriverDx12::createStorageBuffer(const void* data, size_t count, size_t stride, BufferUsage usage) {
	return std::make_shared<StorageBufferDx12>(data, count, stride, usage);
}

void DriverDx12::fillBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size, uint32_t value) {
	auto& native = NativeBuffer(buffer, "fillBuffer");
	if (!size) {
		return;
	}
	if (offset % 4 != 0 || size % 4 != 0) {
		throw std::invalid_argument("fillBuffer: offset and size must be multiples of 4");
	}
	CheckRange(native, offset, size, "fillBuffer");
	// ClearUnorderedAccessViewUint needs a CPU descriptor in a non-shader-visible heap and
	// covers whole views, so a pattern upload + copy is used for arbitrary ranges.
	auto upload = d3dUtil::CreateBuffer(size, D3D12_HEAP_TYPE_UPLOAD);
	void* mapped = nullptr;
	ThrowIfFailed(upload.Get()->Map(0, nullptr, &mapped));
	std::fill_n(static_cast<uint32_t*>(mapped), size / sizeof(uint32_t), value);
	upload.Get()->Unmap(0, nullptr);
	native.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
	mCommandList->CopyBufferRegion(native.getBuffer().Get(), offset, upload.Get(), 0, size);
	// Leave the destination in a writable shader state; later consumers transition from it.
	native.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	destroyDeferred(std::move(upload));
}

void DriverDx12::copyBuffer(const std::shared_ptr<StorageBufferInterface>& source, size_t sourceOffset,
	const std::shared_ptr<StorageBufferInterface>& destination, size_t destinationOffset, size_t size) {
	auto& from = NativeBuffer(source, "copyBuffer");
	auto& to = NativeBuffer(destination, "copyBuffer");
	if (!size) {
		return;
	}
	if (&from == &to) {
		throw std::invalid_argument("copyBuffer: source and destination must be different buffers");
	}
	CheckTransferSource(from, "copyBuffer");
	CheckRange(from, sourceOffset, size, "copyBuffer");
	CheckRange(to, destinationOffset, size, "copyBuffer");
	from.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
	to.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
	mCommandList->CopyBufferRegion(to.getBuffer().Get(), destinationOffset, from.getBuffer().Get(), sourceOffset, size);
	to.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

std::vector<uint8_t> DriverDx12::readBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) {
	auto& native = NativeBuffer(buffer, "readBuffer");
	std::vector<uint8_t> result(size);
	if (!size) {
		return result;
	}
	CheckTransferSource(native, "readBuffer");
	CheckRange(native, offset, size, "readBuffer");
	auto readback = d3dUtil::CreateBuffer(size, D3D12_HEAP_TYPE_READBACK);
	native.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
	mCommandList->CopyBufferRegion(readback.Get(), 0, native.getBuffer().Get(), offset, size);
	flushRecordedCommands();
	void* mapped = nullptr;
	const D3D12_RANGE range { 0, static_cast<SIZE_T>(size) };
	ThrowIfFailed(readback.Get()->Map(0, &range, &mapped));
	std::memcpy(result.data(), mapped, size);
	const D3D12_RANGE written { 0, 0 };
	readback.Get()->Unmap(0, &written);
	return result;
}

void DriverDx12::flushRecordedCommands() {
	// Executes everything recorded in this frame, waits for it and reopens the command list.
	// Pipeline, root signature, heaps and render targets are re-applied by applyState().
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* lists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(1, lists);
	wait();
	beginCommandList(mCommandAllocator.Get(), mCommandList.Get());
	for (auto flag :
		{ Dirty::VIEWPORT, Dirty::SCISSOR, Dirty::PRIMITIVE_MODE, Dirty::RASTERIZATION_MODE, Dirty::VERTEX_BUFFER, Dirty::INDEX_BUFFER }) {
		setDirty(flag);
	}
}

void DriverDx12::drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset, uint32_t drawCount, uint32_t stride) {
	auto& native = NativeBuffer(arguments, "drawIndexedIndirect");
	if (!drawCount) {
		return;
	}
	CheckIndirectArguments(native, offset, drawCount, stride, "drawIndexedIndirect");
	validateVertexDraw({ 1, 0, 1, 0, 0, true });
	applyState();
	native.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	mCommandList->ExecuteIndirect(drawIndexedSignature(stride), drawCount, native.getBuffer().Get(), offset, nullptr, 0);
}

void DriverDx12::drawIndexedIndirectCount(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset,
	const std::shared_ptr<StorageBufferInterface>& count, size_t countOffset, uint32_t maxDrawCount, uint32_t stride) {
	auto& native = NativeBuffer(arguments, "drawIndexedIndirectCount");
	auto& counter = NativeBuffer(count, "drawIndexedIndirectCount");
	if (!maxDrawCount) {
		return;
	}
	CheckIndirectArguments(native, offset, maxDrawCount, stride, "drawIndexedIndirectCount");
	CheckUsage(counter, BufferUsage::INDIRECT, "drawIndexedIndirectCount");
	if (countOffset % 4 != 0) {
		throw std::invalid_argument("drawIndexedIndirectCount: count offset must be a multiple of 4");
	}
	CheckRange(counter, countOffset, sizeof(uint32_t), "drawIndexedIndirectCount");
	validateVertexDraw({ 1, 0, 1, 0, 0, true });
	applyState();
	native.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	counter.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	mCommandList->ExecuteIndirect(
		drawIndexedSignature(stride), maxDrawCount, native.getBuffer().Get(), offset, counter.getBuffer().Get(), countOffset);
}

void DriverDx12::dispatchIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset) {
	auto& native = NativeBuffer(arguments, "dispatchIndirect");
	CheckUsage(native, BufferUsage::INDIRECT, "dispatchIndirect");
	if (offset % 4 != 0) {
		throw std::invalid_argument("dispatchIndirect: offset must be a multiple of 4");
	}
	CheckRange(native, offset, sizeof(DispatchIndirectCommand), "dispatchIndirect");
	applyState();
	native.transition(mCommandList.Get(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	mCommandList->ExecuteIndirect(mDispatchSignature.Get(), 1, native.getBuffer().Get(), offset, nullptr, 0);
	const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
	mCommandList->ResourceBarrier(1, &barrier);
}
}
#endif
