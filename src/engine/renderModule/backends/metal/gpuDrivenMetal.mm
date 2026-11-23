#include "driverMetal.h"
#ifdef METAL_BACKEND

#include "gpuMemoryMetal.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace IKIGAI::RENDER {
namespace {
	// The engine's portable Metal profile. Draws are encoded one at a time, and
	// indirect dispatch producers must respect the same grid limit as direct dispatch.
	constexpr uint32_t MaxDraws = 65535;
	constexpr uint32_t MaxWorkGroups = 65535;
	// Conservative execution-range limit for the supported Apple GPU profile.
	constexpr uint32_t DrawsPerRange = 1024;
	constexpr size_t MaxPendingReadbacks = 64;

	static_assert(sizeof(DrawIndexedIndirectCommand) == sizeof(MTLDrawIndexedPrimitivesIndirectArguments));
	static_assert(offsetof(DrawIndexedIndirectCommand, IndexCount) == offsetof(MTLDrawIndexedPrimitivesIndirectArguments, indexCount));
	static_assert(offsetof(DrawIndexedIndirectCommand, InstanceCount) == offsetof(MTLDrawIndexedPrimitivesIndirectArguments, instanceCount));
	static_assert(offsetof(DrawIndexedIndirectCommand, FirstIndex) == offsetof(MTLDrawIndexedPrimitivesIndirectArguments, indexStart));
	static_assert(offsetof(DrawIndexedIndirectCommand, VertexOffset) == offsetof(MTLDrawIndexedPrimitivesIndirectArguments, baseVertex));
	static_assert(offsetof(DrawIndexedIndirectCommand, FirstInstance) == offsetof(MTLDrawIndexedPrimitivesIndirectArguments, baseInstance));
	static_assert(sizeof(DispatchIndirectCommand) == sizeof(MTLDispatchThreadgroupsIndirectArguments));

	constexpr auto CountDrawSource = R"(
#include <metal_stdlib>
#include <metal_command_buffer>
using namespace metal;

struct Resources { command_buffer commands [[id(0)]]; };
struct Draw { uint indexCount, instanceCount, firstIndex; int baseVertex; uint baseInstance; };
struct Parameters { uint capacity, stride, primitive, indexSize; ulong indexCapacity; uint drawsPerRange; };

kernel void encodeDraws(const device Resources& resources [[buffer(0)]], const device uchar* draws [[buffer(1)]],
    const device uint& count [[buffer(2)]], device uint2* ranges [[buffer(3)]], device uchar* indices [[buffer(4)]],
    constant Parameters& params [[buffer(5)]], uint i [[thread_position_in_grid]]) {
    uint active = min(count, params.capacity);
    if (i % params.drawsPerRange == 0) {
        uint length = active > i ? min(active - i, params.drawsPerRange) : 0;
        ranges[i / params.drawsPerRange] = uint2(i, length);
    }
    if (i >= active) return;
    Draw draw = *reinterpret_cast<const device Draw*>(draws + ulong(i) * params.stride);
    if (!draw.instanceCount || !draw.indexCount || ulong(draw.firstIndex) + draw.indexCount > params.indexCapacity) return;
    render_command command(resources.commands, i);
    auto primitive = static_cast<primitive_type>(params.primitive);
    if (params.indexSize == 2)
        command.draw_indexed_primitives(primitive, draw.indexCount, reinterpret_cast<device ushort*>(indices) + draw.firstIndex,
            draw.instanceCount, draw.baseVertex, draw.baseInstance);
    else
        command.draw_indexed_primitives(primitive, draw.indexCount, reinterpret_cast<device uint*>(indices) + draw.firstIndex,
            draw.instanceCount, draw.baseVertex, draw.baseInstance);
}
)";

	constexpr auto TransferSource = R"(
#include <metal_stdlib>
using namespace metal;

kernel void ikigaiFillWords(device uint* destination [[buffer(0)]], constant ulong* range [[buffer(1)]],
    constant uint& value [[buffer(2)]], uint index [[thread_position_in_grid]], uint width [[threads_per_grid]]) {
    for (ulong word = index; word < range[1]; word += width)
        destination[range[0] + word] = value;
}

kernel void ikigaiCopyBytes(const device uchar* source [[buffer(0)]], device uchar* destination [[buffer(1)]],
    constant ulong* range [[buffer(2)]], uint index [[thread_position_in_grid]], uint width [[threads_per_grid]]) {
    for (ulong byte = index; byte < range[2]; byte += width)
        destination[range[1] + byte] = source[range[0] + byte];
}
)";

	StorageBufferMetal& NativeBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, const char* operation) {
		auto* native = dynamic_cast<StorageBufferMetal*>(buffer.get());
		if (!native || !native->getBuffer()) {
			throw std::invalid_argument(std::string(operation) + ": missing Metal storage buffer");
		}
		return *native;
	}

	void CheckRange(const StorageBufferMetal& buffer, size_t offset, size_t size, const char* operation) {
		const auto length = buffer.getSizeByte();
		if (offset > length || size > length - offset) {
			throw std::invalid_argument(std::string(operation) + ": range exceeds buffer size");
		}
	}

	void CheckTransferSource(const StorageBufferMetal& buffer, const char* operation) {
		if (!HasBufferUsage(buffer.getUsage(), BufferUsage::TRANSFER_SRC | BufferUsage::READBACK)) {
			throw std::invalid_argument(std::string(operation) + ": buffer needs TRANSFER_SRC or READBACK usage");
		}
	}

	void CheckIndirect(const StorageBufferMetal& buffer, size_t offset, const char* operation) {
		if (!HasBufferUsage(buffer.getUsage(), BufferUsage::INDIRECT) || offset % 4 != 0) {
			throw std::invalid_argument(std::string(operation) + ": requires INDIRECT usage and a four-byte aligned offset");
		}
	}

	bool SupportsIndirect(id<MTLDevice> device) {
		if (@available(macOS 10.15, iOS 13.0, *)) {
			return [device supportsFamily:MTLGPUFamilyApple3] || [device supportsFamily:MTLGPUFamilyMac2];
		}
		return false;
	}

	void DispatchBufferKernel(id<MTLComputeCommandEncoder> encoder, id<MTLComputePipelineState> pipeline, size_t count) {
		const NSUInteger width = std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup, 256);
		const NSUInteger groups = std::min<NSUInteger>(1 + (count - 1) / width, MaxWorkGroups);
		[encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
	}

	void CheckCompletion(id<MTLCommandBuffer> command, const char* operation) {
		if (command.status != MTLCommandBufferStatusCompleted) {
			const char* reason = command.error.localizedDescription.UTF8String;
			throw std::runtime_error(std::string(operation) + ": " + (reason ? reason : "Metal command buffer failed"));
		}
	}
}

GpuFeatures DriverMetal::gpuFeatures() const {
	GpuFeatures result;
	if (!mDevice || !mCommandQueue) {
		return result;
	}
	if (@available(macOS 12.0, iOS 15.0, *)) {
		result.Compute = true;
	}
	result.BufferUsageFlags = true;
	result.BufferTransfer = true;
	result.BufferReadback = true;
	result.DrawIndirect = SupportsIndirect(mDevice);
	if (@available(macOS 12.0, iOS 15.0, *)) {
		result.DrawIndirectCount = [mDevice supportsFamily:MTLGPUFamilyApple4] && !std::getenv("IKIGAI_DISABLE_DRAW_INDIRECT_COUNT");
	}
	result.DispatchIndirect = result.DrawIndirect;
	result.MaxDrawIndirectCount = result.DrawIndirect ? MaxDraws : 0;
	result.MaxComputeWorkGroupCountX = MaxWorkGroups;
	result.MaxComputeWorkGroupCountY = MaxWorkGroups;
	result.MaxComputeWorkGroupCountZ = MaxWorkGroups;
	return result;
}

std::shared_ptr<StorageBufferInterface> DriverMetal::createStorageBuffer(const void* data, size_t size, size_t stride, BufferUsage usage) {
	return std::make_shared<StorageBufferMetal>(size, stride, usage, data);
}

id<MTLCommandBuffer> DriverMetal::beginGpuCommands(const char* operation) {
	requireOwnerThread();
	if (!mCurrentCommandBuffer) {
		begin();
	}
	if (!mCurrentCommandBuffer) {
		throw std::runtime_error(std::string(operation) + ": Metal could not open a frame command buffer");
	}
	// Separating passes lets Metal order tracked resources on this command queue.
	endPass();
	return mCurrentCommandBuffer;
}

id<MTLComputePipelineState> DriverMetal::bufferTransferPipeline(bool fill) {
	NSError* error = nil;
	if (!mBufferTransferLibrary) {
		mBufferTransferLibrary = [mDevice newLibraryWithSource:[NSString stringWithUTF8String:TransferSource] options:nil error:&error];
		if (!mBufferTransferLibrary) {
			const char* reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(std::string("Metal buffer transfer shader: ") + (reason ? reason : "compilation failed"));
		}
	}
	auto& pipeline = fill ? mBufferFillPipeline : mBufferCopyPipeline;
	if (!pipeline) {
		id<MTLFunction> function = [mBufferTransferLibrary newFunctionWithName:fill ? @"ikigaiFillWords" : @"ikigaiCopyBytes"];
		pipeline = [mDevice newComputePipelineStateWithFunction:function error:&error];
		if (!pipeline) {
			const char* reason = error.localizedDescription.UTF8String;
			throw std::runtime_error(std::string("Metal buffer transfer pipeline: ") + (reason ? reason : "creation failed"));
		}
#if IKIGAI_ENABLE_MEMORY_TRACKING
		TrackGpuObjectMetal(pipeline, mDevice, PROFILER::GpuResourceKind::Pipeline, "Buffer transfer pipeline");
#endif
	}
	return pipeline;
}

void DriverMetal::fillBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size, uint32_t value) {
	const BufferFill fill{buffer, offset, size, value};
	fillBuffers(std::span<const BufferFill>(&fill, 1));
}

void DriverMetal::fillBuffers(std::span<const BufferFill> fills) {
	requireOwnerThread();
	id<MTLComputePipelineState> pipeline = nil;
	bool hasWork = false;
	for (const auto& fill : fills) {
		const auto& buffer = NativeBuffer(fill.Buffer, "fillBuffer");
		if (!fill.Size) {
			continue;
		}
		if ((fill.Offset | fill.Size) % 4 != 0) {
			throw std::invalid_argument("fillBuffer: offset and size must be multiples of four");
		}
		CheckRange(buffer, fill.Offset, fill.Size, "fillBuffer");
		const auto byte = static_cast<uint8_t>(fill.Value);
		if (fill.Value != uint32_t(byte) * 0x01010101u) {
			pipeline = bufferTransferPipeline(true);
		}
		hasWork = true;
	}
	if (!hasWork) {
		return;
	}
	id<MTLCommandBuffer> command = beginGpuCommands("fillBuffer");
	for (const auto& fill : fills) {
		if (!fill.Size) {
			continue;
		}
		id<MTLBuffer> destination = NativeBuffer(fill.Buffer, "fillBuffer").getBuffer();
		const auto byte = static_cast<uint8_t>(fill.Value);
		if (fill.Value == uint32_t(byte) * 0x01010101u) {
			id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
			[blit fillBuffer:destination range:NSMakeRange(fill.Offset, fill.Size) value:byte];
			[blit endEncoding];
		} else {
			const uint64_t range[] = {fill.Offset / 4, fill.Size / 4};
			id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
			[encoder setComputePipelineState:pipeline];
			[encoder setBuffer:destination offset:0 atIndex:0];
			[encoder setBytes:range length:sizeof(range) atIndex:1];
			[encoder setBytes:&fill.Value length:sizeof(fill.Value) atIndex:2];
			DispatchBufferKernel(encoder, pipeline, fill.Size / 4);
			[encoder endEncoding];
		}
	}
}

void DriverMetal::encodeBufferCopy(id<MTLCommandBuffer> command, id<MTLBuffer> source, size_t sourceOffset,
	id<MTLBuffer> destination, size_t destinationOffset, size_t size) {
	if ((sourceOffset | destinationOffset | size) % 4 == 0) {
		id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
		[blit copyFromBuffer:source sourceOffset:sourceOffset toBuffer:destination destinationOffset:destinationOffset size:size];
		[blit endEncoding];
		return;
	}
	// The macOS blit encoder accepts word-aligned copies. Preserve exact byte
	// ranges for the portable API without reading either buffer on the CPU.
	id<MTLComputePipelineState> pipeline = bufferTransferPipeline(false);
	id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
	const uint64_t range[] = {sourceOffset, destinationOffset, size};
	[encoder setComputePipelineState:pipeline];
	[encoder setBuffer:source offset:0 atIndex:0];
	[encoder setBuffer:destination offset:0 atIndex:1];
	[encoder setBytes:range length:sizeof(range) atIndex:2];
	DispatchBufferKernel(encoder, pipeline, size);
	[encoder endEncoding];
}

void DriverMetal::copyBuffer(const std::shared_ptr<StorageBufferInterface>& source, size_t sourceOffset,
	const std::shared_ptr<StorageBufferInterface>& destination, size_t destinationOffset, size_t size) {
	auto& from = NativeBuffer(source, "copyBuffer");
	auto& to = NativeBuffer(destination, "copyBuffer");
	if (!size) {
		return;
	}
	if (from.getBuffer() == to.getBuffer()) {
		throw std::invalid_argument("copyBuffer: source and destination must be different buffers");
	}
	CheckTransferSource(from, "copyBuffer");
	CheckRange(from, sourceOffset, size, "copyBuffer");
	CheckRange(to, destinationOffset, size, "copyBuffer");
	encodeBufferCopy(beginGpuCommands("copyBuffer"), from.getBuffer(), sourceOffset, to.getBuffer(), destinationOffset, size);
}

void DriverMetal::uploadStorageBuffer(id<MTLBuffer> destination, const void* data, size_t size, size_t offset) {
	requireOwnerThread();
	id<MTLBuffer> staging = AllocateBufferMetal(mDevice, size, MTLResourceStorageModeShared | MTLResourceHazardTrackingModeTracked,
		PROFILER::GpuResourceKind::Staging);
	if (!staging) {
		throw std::runtime_error("Metal storage upload staging allocation failed");
	}
	std::memcpy(staging.contents, data, size);
	const bool active = mCurrentCommandBuffer != nil;
	id<MTLCommandBuffer> command = active ? beginGpuCommands("storage buffer upload") : [mCommandQueue commandBuffer];
	if (!command) {
		throw std::runtime_error("Metal storage upload requires a command buffer");
	}
	encodeBufferCopy(command, staging, 0, destination, offset, size);
	if (!active) {
		[command commit];
	}
}

DriverMetal::BufferReadback DriverMetal::encodeBufferReadback(id<MTLCommandBuffer> command, id<MTLBuffer> source, size_t offset, size_t size) {
	const size_t alignedOffset = offset & ~size_t{3};
	const size_t alignedSize = ((offset + size + 3) & ~size_t{3}) - alignedOffset;
	id<MTLBuffer> staging = AllocateBufferMetal(mDevice, alignedSize, MTLResourceStorageModeShared | MTLResourceHazardTrackingModeTracked,
		PROFILER::GpuResourceKind::Staging);
	if (!staging) {
		throw std::runtime_error("Metal buffer readback staging allocation failed");
	}
	encodeBufferCopy(command, source, alignedOffset, staging, 0, alignedSize);
	return {staging, offset - alignedOffset, size};
}

std::vector<uint8_t> DriverMetal::readBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) {
	requireOwnerThread();
	const auto& native = NativeBuffer(buffer, "readBuffer");
	if (!size) {
		return {};
	}
	CheckTransferSource(native, "readBuffer");
	CheckRange(native, offset, size, "readBuffer");
	const bool active = mCurrentCommandBuffer != nil;
	id<MTLCommandBuffer> command = active ? beginGpuCommands("readBuffer") : [mCommandQueue commandBuffer];
	if (!command) {
		throw std::runtime_error("readBuffer: Metal could not open a command buffer");
	}
	const auto copy = encodeBufferReadback(command, native.getBuffer(), offset, size);
	[command commit];
	[command waitUntilCompleted];
	if (active) {
		mCurrentCommandBuffer = [mCommandQueue commandBuffer];
	}
	CheckCompletion(command, "readBuffer");
	const auto* data = static_cast<const uint8_t*>(copy.Staging.contents) + copy.Offset;
	return {data, data + copy.Size};
}

uint64_t DriverMetal::requestBufferReadback(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) {
	const auto& native = NativeBuffer(buffer, "requestBufferReadback");
	if (!size) {
		throw std::invalid_argument("requestBufferReadback: empty range");
	}
	CheckTransferSource(native, "requestBufferReadback");
	CheckRange(native, offset, size, "requestBufferReadback");
	id<MTLCommandBuffer> command = beginGpuCommands("requestBufferReadback");
	while (mPendingBufferReadbacks.size() >= MaxPendingReadbacks
		&& mPendingBufferReadbacks.front().Status->load(std::memory_order_acquire) >= MTLCommandBufferStatusCompleted) {
		mPendingBufferReadbacks.pop_front();
	}
	auto status = std::make_shared<std::atomic<MTLCommandBufferStatus>>(MTLCommandBufferStatusNotEnqueued);
	const auto copy = encodeBufferReadback(command, native.getBuffer(), offset, size);
	const uint64_t ticket = mNextBufferReadbackTicket++;
	mPendingBufferReadbacks.push_back({copy, status, ticket});
	[command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
		status->store(finished.status, std::memory_order_release);
	}];
	return ticket;
}

bool DriverMetal::collectBufferReadback(uint64_t ticket, std::vector<uint8_t>& bytes) {
	requireOwnerThread();
	const auto pending = std::find_if(mPendingBufferReadbacks.begin(), mPendingBufferReadbacks.end(),
		[ticket](const auto& entry) { return entry.Ticket == ticket; });
	if (pending == mPendingBufferReadbacks.end()) {
		return false;
	}
	const auto status = pending->Status->load(std::memory_order_acquire);
	if (status < MTLCommandBufferStatusCompleted) {
		return false;
	}
	if (status == MTLCommandBufferStatusError) {
		mPendingBufferReadbacks.erase(pending);
		throw std::runtime_error("collectBufferReadback: Metal command buffer failed");
	}
	const auto& copy = pending->Copy;
	const auto* data = static_cast<const uint8_t*>(copy.Staging.contents) + copy.Offset;
	bytes.assign(data, data + copy.Size);
	mPendingBufferReadbacks.erase(pending);
	return true;
}

void DriverMetal::checkComputeGrid(MTLSize groups) {
	if (groups.width > MaxWorkGroups || groups.height > MaxWorkGroups || groups.depth > MaxWorkGroups) {
		throw std::invalid_argument("Metal compute grid exceeds the supported workgroup count");
	}
}

void DriverMetal::checkComputeThreads(id<MTLComputePipelineState> state, MTLSize threads) const {
	const auto limits = mDevice.maxThreadsPerThreadgroup;
	const auto total = state.maxTotalThreadsPerThreadgroup;
	if (!threads.width || !threads.height || !threads.depth
		|| threads.width > limits.width || threads.height > limits.height || threads.depth > limits.depth
		|| threads.width > total / threads.height / threads.depth) {
		throw std::invalid_argument("Metal compute local size exceeds threadgroup limits");
	}
}

void DriverMetal::encodeDispatch(MTLSize groups, id<MTLBuffer> arguments, size_t offset) {
	requireOwnerThread();
	const auto shader = mCurrentState.mShader;
	const auto state = shader ? computePipeline(shader->getComputeFunction()) : nil;
	if (!state) {
		throw std::runtime_error("Missing Metal compute pipeline");
	}
	const auto& localSize = shader->getReflection().mLocalSize;
	const auto threads = MTLSizeMake(localSize[0], localSize[1], localSize[2]);
	checkComputeThreads(state, threads);
	id<MTLCommandBuffer> command = beginGpuCommands("dispatch");
	id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
	[encoder setComputePipelineState:state];
	try {
		bindComputeResources(encoder);
		if (arguments) {
			[encoder useResource:arguments usage:MTLResourceUsageRead];
			[encoder dispatchThreadgroupsWithIndirectBuffer:arguments indirectBufferOffset:offset threadsPerThreadgroup:threads];
		} else {
			[encoder dispatchThreadgroups:groups threadsPerThreadgroup:threads];
		}
	} catch (...) {
		[encoder endEncoding];
		throw;
	}
	[encoder endEncoding];
}

void DriverMetal::dispatch(uint32_t x, uint32_t y, uint32_t z) {
	if (!x || !y || !z) {
		return;
	}
	const auto groups = MTLSizeMake(x, y, z);
	checkComputeGrid(groups);
	encodeDispatch(groups);
}

void DriverMetal::dispatchIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset) {
	if (!SupportsIndirect(mDevice)) {
		throw UnsupportedGpuFeature("Metal device does not support indirect dispatch");
	}
	const auto& buffer = NativeBuffer(arguments, "dispatchIndirect");
	CheckIndirect(buffer, offset, "dispatchIndirect");
	CheckRange(buffer, offset, sizeof(DispatchIndirectCommand), "dispatchIndirect");
	encodeDispatch(MTLSizeMake(0, 0, 0), buffer.getBuffer(), offset);
}

void DriverMetal::drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset, uint32_t drawCount, uint32_t stride) {
	requireOwnerThread();
	const auto& buffer = NativeBuffer(arguments, "drawIndexedIndirect");
	if (!drawCount) {
		return;
	}
	if (!SupportsIndirect(mDevice)) {
		throw UnsupportedGpuFeature("Metal device does not support indexed indirect draw");
	}
	CheckIndirect(buffer, offset, "drawIndexedIndirect");
	if (drawCount > MaxDraws || stride % 4 != 0 || stride < sizeof(DrawIndexedIndirectCommand)) {
		throw std::invalid_argument("drawIndexedIndirect: invalid draw count or command stride");
	}
	const uint64_t size = uint64_t(drawCount - 1) * stride + sizeof(DrawIndexedIndirectCommand);
	CheckRange(buffer, offset, size, "drawIndexedIndirect");
	if (!mCurrentState.mShader || !mIndexBuffer || !mIndexBuffer->getBuffer()
		|| (mIndexBuffer->getStride() != sizeof(uint16_t) && mIndexBuffer->getStride() != sizeof(uint32_t))) {
		throw std::runtime_error("drawIndexedIndirect: missing shader or valid index buffer");
	}
	if (mCurrentState.mShader->hasTessellation() || mCurrentState.mShader->hasGeometry()) {
		throw UnsupportedGpuFeature("Metal indexed indirect drawing requires a vertex/fragment pipeline");
	}
	validateVertexDraw({1, 0, 1, 0, 0, true});
	ensurePass();
	if (!ensurePipeline()) {
		throw std::runtime_error("drawIndexedIndirect: Metal render pipeline unavailable");
	}
	applyFixedState();
	bindResources();
	const MTLIndexType type = mIndexBuffer->getStride() == sizeof(uint16_t) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
	const auto primitive = currentPrimitiveType();
	id<MTLBuffer> indirect = buffer.getBuffer();
	id<MTLBuffer> indices = mIndexBuffer->getBuffer();
	[mCurrentEncoder useResource:indirect usage:MTLResourceUsageRead stages:MTLRenderStageVertex];
	for (uint32_t draw = 0; draw < drawCount; ++draw) {
		[mCurrentEncoder drawIndexedPrimitives:primitive indexType:type indexBuffer:indices indexBufferOffset:0
			indirectBuffer:indirect indirectBufferOffset:offset + size_t(draw) * stride];
	}
}

void DriverMetal::ensureCountDrawResources(uint32_t capacity) {
	if (mCountDrawCommands && mCountDrawCommands.size >= capacity) return;
	NSError* error = nil;
	if (!mCountDrawPipeline) {
		id<MTLLibrary> library = [mDevice newLibraryWithSource:@(CountDrawSource) options:nil error:&error];
		id<MTLFunction> function = [library newFunctionWithName:@"encodeDraws"];
		if (!function) throw std::runtime_error(std::string("Metal ICB shader: ") + (error.localizedDescription.UTF8String ?: "missing kernel"));
		mCountDrawArgumentEncoder = [function newArgumentEncoderWithBufferIndex:0];
		if (!mCountDrawArgumentEncoder) throw std::runtime_error("Metal ICB argument encoder creation failed");
		mCountDrawPipeline = [mDevice newComputePipelineStateWithFunction:function error:&error];
		if (!mCountDrawPipeline) throw std::runtime_error(std::string("Metal ICB pipeline: ") + (error.localizedDescription.UTF8String ?: "creation failed"));
#if IKIGAI_ENABLE_MEMORY_TRACKING
		TrackGpuObjectMetal(mCountDrawPipeline, mDevice, PROFILER::GpuResourceKind::Pipeline, "ICB encoder");
#endif
	}
	MTLIndirectCommandBufferDescriptor* desc = [MTLIndirectCommandBufferDescriptor new];
	desc.commandTypes = MTLIndirectCommandTypeDrawIndexed;
	desc.inheritPipelineState = YES;
	desc.inheritBuffers = YES;
	auto commands = [mDevice newIndirectCommandBufferWithDescriptor:desc maxCommandCount:capacity
		options:MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked];
	auto encoder = mCountDrawArgumentEncoder;
	auto bindings = AllocateBufferMetal(mDevice, encoder.encodedLength, MTLResourceStorageModeShared);
	const size_t rangeCount = (capacity + DrawsPerRange - 1) / DrawsPerRange;
	auto range = AllocateBufferMetal(mDevice, rangeCount * sizeof(MTLIndirectCommandBufferExecutionRange), MTLResourceStorageModePrivate);
	if (!commands || !bindings || !range) throw std::runtime_error("Metal ICB allocation failed");
#if IKIGAI_ENABLE_MEMORY_TRACKING
	TrackGpuMemoryMetal(commands, PROFILER::GpuResourceKind::Buffer, "Indexed draw ICB");
#endif
	[encoder setArgumentBuffer:bindings offset:0];
	[encoder setIndirectCommandBuffer:commands atIndex:0];
	// Bindings are immutable. Prior submissions retain their resources on growth.
	mCountDrawCommands = commands;
	mCountDrawBindings = bindings;
	mCountDrawRange = range;
}

void DriverMetal::drawIndexedIndirectCount(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset,
	const std::shared_ptr<StorageBufferInterface>& count, size_t countOffset, uint32_t maxDrawCount, uint32_t stride) {
	requireOwnerThread();
	auto& draws = NativeBuffer(arguments, "drawIndexedIndirectCount");
	auto& counter = NativeBuffer(count, "drawIndexedIndirectCount");
	if (!maxDrawCount) return;
	if (!gpuFeatures().DrawIndirectCount) throw UnsupportedGpuFeature("Metal device does not support indirect count draws");
	CheckIndirect(draws, offset, "drawIndexedIndirectCount");
	CheckIndirect(counter, countOffset, "drawIndexedIndirectCount");
	CheckRange(counter, countOffset, sizeof(uint32_t), "drawIndexedIndirectCount");
	if (maxDrawCount > MaxDraws || stride % 4 || stride < sizeof(DrawIndexedIndirectCommand))
		throw std::invalid_argument("drawIndexedIndirectCount: invalid maximum or command stride");
	CheckRange(draws, offset, uint64_t(maxDrawCount - 1) * stride + sizeof(DrawIndexedIndirectCommand), "drawIndexedIndirectCount");
	if (!mCurrentState.mShader || !mIndexBuffer || !mIndexBuffer->getBuffer()
		|| (mIndexBuffer->getStride() != 2 && mIndexBuffer->getStride() != 4))
		throw std::runtime_error("drawIndexedIndirectCount: missing shader or valid index buffer");
	if (mCurrentState.mShader->hasTessellation() || mCurrentState.mShader->hasGeometry())
		throw UnsupportedGpuFeature("Metal indexed indirect drawing requires a vertex/fragment pipeline");
	validateVertexDraw({1, 0, 1, 0, 0, true});
	ensureCountDrawResources(maxDrawCount);
	auto command = beginGpuCommands("drawIndexedIndirectCount");
	auto reset = [command blitCommandEncoder];
	[reset resetCommandsInBuffer:mCountDrawCommands withRange:NSMakeRange(0, maxDrawCount)];
	[reset endEncoding];

	id<MTLBuffer> indices = mIndexBuffer->getBuffer();
	struct { uint32_t capacity, stride, primitive, indexSize; uint64_t indexCapacity; uint32_t drawsPerRange; } params {
		maxDrawCount, stride, uint32_t(currentPrimitiveType()), uint32_t(mIndexBuffer->getStride()), mIndexBuffer->getSize(), DrawsPerRange
	};
	auto compute = [command computeCommandEncoder];
	[compute setComputePipelineState:mCountDrawPipeline];
	[compute setBuffer:mCountDrawBindings offset:0 atIndex:0];
	[compute setBuffer:draws.getBuffer() offset:offset atIndex:1];
	[compute setBuffer:counter.getBuffer() offset:countOffset atIndex:2];
	[compute setBuffer:mCountDrawRange offset:0 atIndex:3];
	[compute setBuffer:indices offset:0 atIndex:4];
	[compute setBytes:&params length:sizeof(params) atIndex:5];
	[compute useResource:mCountDrawCommands usage:MTLResourceUsageWrite];
	[compute dispatchThreads:MTLSizeMake(maxDrawCount, 1, 1) threadsPerThreadgroup:MTLSizeMake(mCountDrawPipeline.threadExecutionWidth, 1, 1)];
	[compute endEncoding];

	ensurePass();
	if (!ensurePipeline(true)) throw std::runtime_error("drawIndexedIndirectCount: Metal render pipeline unavailable");
	applyFixedState();
	bindResources();
	[mCurrentEncoder useResource:indices usage:MTLResourceUsageRead stages:MTLRenderStageVertex];
	for (uint32_t first = 0; first < maxDrawCount; first += DrawsPerRange) {
		const NSUInteger rangeOffset = (first / DrawsPerRange) * sizeof(MTLIndirectCommandBufferExecutionRange);
		[mCurrentEncoder executeCommandsInBuffer:mCountDrawCommands indirectBuffer:mCountDrawRange indirectBufferOffset:rangeOffset];
	}
}
}
#endif
