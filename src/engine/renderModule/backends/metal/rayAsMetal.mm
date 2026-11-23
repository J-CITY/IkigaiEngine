#if defined(METAL_BACKEND) && USE_RAY_TRACING

#include "rayAsMetal.h"

#include "driverMetal.h"
#include "gpuMemoryMetal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace IKIGAI::RENDER {
namespace {

using PROFILER::GpuResourceKind;

id<MTLBuffer> allocateASBuffer(id<MTLDevice> device, size_t bytes, MTLResourceOptions options, const char* label) {
	id<MTLBuffer> buffer = AllocateBufferMetal(device, bytes, options, GpuResourceKind::Buffer, label);
	if (!buffer)
		throw std::runtime_error(std::string("Metal allocation failed for ") + label);
	return buffer;
}

id<MTLAccelerationStructure> encodeAccelerationStructure(const DriverMetal& driver, MTLAccelerationStructureDescriptor* descriptor,
	id<MTLAccelerationStructure> refitSource = nil) {
	id<MTLDevice> device = driver.getDevice();
	const auto sizes = [device accelerationStructureSizesWithDescriptor:descriptor];
	id<MTLAccelerationStructure> result = refitSource;
	if (!result) {
		result = [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
		if (!result)
			throw std::runtime_error("Metal acceleration structure allocation failed");
#if IKIGAI_ENABLE_MEMORY_TRACKING
		TrackGpuMemoryMetal(result, GpuResourceKind::AccelerationStructure, "Ray tracing acceleration structure");
#endif
	}
	const size_t scratchBytes = refitSource ? sizes.refitScratchBufferSize : sizes.buildScratchBufferSize;
	id<MTLBuffer> scratch = allocateASBuffer(device, std::max<size_t>(16, scratchBytes), MTLResourceStorageModePrivate, "Acceleration structure scratch");

	id<MTLCommandBuffer> commands = driver.getCurrentCommandBuffer();
	const bool standalone = !commands;
	if (standalone)
		commands = [driver.getCommandQueue() commandBuffer];
	id<MTLAccelerationStructureCommandEncoder> encoder = [commands accelerationStructureCommandEncoder];
	if (!encoder)
		throw std::runtime_error("Metal acceleration structure encoder allocation failed");
	if (refitSource)
		[encoder refitAccelerationStructure:result descriptor:descriptor destination:nil scratchBuffer:scratch scratchBufferOffset:0];
	else
		[encoder buildAccelerationStructure:result descriptor:descriptor scratchBuffer:scratch scratchBufferOffset:0];
	[encoder endEncoding];
	// The descriptor owns the input buffers and BLAS handles for this build or refit.
	[commands addCompletedHandler:^(id<MTLCommandBuffer>) { (void)descriptor; }];
	if (standalone) {
		[commands commit];
		[commands waitUntilCompleted];
		if (commands.status != MTLCommandBufferStatusCompleted)
			throw std::runtime_error(std::string("Metal acceleration structure encoding failed: ") + commands.error.localizedDescription.UTF8String);
	}
	return result;
}

bool bufferRangeFits(uint64_t offset, uint64_t required, size_t size) {
	return offset <= size && required <= size - offset;
}

void validateTriangleBuffers(id<MTLDevice> device, const RayTracingGeometry& input,
	const std::shared_ptr<VertexBufferMetal>& vertices, const std::shared_ptr<IndexBufferMetal>& indices) {
	constexpr size_t positionBytes = 3 * sizeof(float);
	if (!vertices || vertices->getBuffer().device != device || !input.VertexCount || input.VertexStride < positionBytes
		|| input.VertexStride % sizeof(float) || input.VertexOffset % input.VertexStride
		|| !bufferRangeFits(input.VertexOffset, uint64_t(input.VertexCount - 1) * input.VertexStride + positionBytes, vertices->getSizeByte()))
		throw std::runtime_error("Invalid Metal ray tracing vertex buffer, range, or alignment");

	const uint32_t count = indices ? input.IndexCount : input.VertexCount;
	if (!count || count % 3)
		throw std::runtime_error("Metal ray tracing geometry requires complete triangles");

	if (input.Indices && (!indices || indices->getBuffer().device != device || (input.IndexStride != 2 && input.IndexStride != 4)
		|| input.IndexOffset % input.IndexStride
		|| !bufferRangeFits(input.IndexOffset, uint64_t(count) * input.IndexStride, indices->getSizeByte())))
		throw std::runtime_error("Invalid Metal ray tracing index buffer, range, or alignment");
}

MTLAccelerationStructureTriangleGeometryDescriptor* triangleDescriptor(id<MTLDevice> device, const RayTracingGeometry& input) {
	auto vertices = std::dynamic_pointer_cast<VertexBufferMetal>(input.Vertices);
	auto indices = std::dynamic_pointer_cast<IndexBufferMetal>(input.Indices);
	validateTriangleBuffers(device, input, vertices, indices);

	auto* descriptor = [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
	descriptor.vertexBuffer = vertices->getBuffer();
	descriptor.vertexBufferOffset = input.VertexOffset;
	// Metal's default vertex format is a packed float3.
	descriptor.vertexStride = input.VertexStride;
	descriptor.triangleCount = (indices ? input.IndexCount : input.VertexCount) / 3;
	if (indices) {
		descriptor.indexBuffer = indices->getBuffer();
		descriptor.indexBufferOffset = input.IndexOffset;
		descriptor.indexType = input.IndexStride == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
	}
	return descriptor;
}

MTLAccelerationStructureBoundingBoxGeometryDescriptor* boundingBoxDescriptor(id<MTLDevice> device, const RayTracingGeometry& input) {
	std::vector<MTLAxisAlignedBoundingBox> bounds;
	bounds.reserve(input.Aabbs.size());
	for (const auto& box : input.Aabbs) {
		for (size_t axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(box[axis]) || !std::isfinite(box[axis + 3]) || box[axis] > box[axis + 3])
				throw std::runtime_error("Invalid Metal ray tracing AABB");
		}
		bounds.emplace_back(MTLPackedFloat3(box[0], box[1], box[2]), MTLPackedFloat3(box[3], box[4], box[5]));
	}
	const size_t bytes = bounds.size() * sizeof(bounds.front());
	auto* descriptor = [MTLAccelerationStructureBoundingBoxGeometryDescriptor descriptor];
	descriptor.boundingBoxBuffer = allocateASBuffer(device, bytes, MTLResourceStorageModeShared, "Acceleration structure bounding boxes");
	std::memcpy(descriptor.boundingBoxBuffer.contents, bounds.data(), bytes);
	descriptor.boundingBoxStride = sizeof(bounds.front());
	descriptor.boundingBoxCount = bounds.size();
	return descriptor;
}

MTLInstanceAccelerationStructureDescriptor* instanceDescriptor(const DriverMetal& driver, const std::vector<RayTracingInstance>& source,
	bool allowUpdate) API_AVAILABLE(macos(12.0), ios(15.0)) {
	if (source.size() > std::numeric_limits<uint32_t>::max())
		throw std::runtime_error("Metal ray tracing instance count exceeds device index range");

	auto* bottomLevels = [NSMutableArray<id<MTLAccelerationStructure>> array];
	std::unordered_map<const AccelerationStructureMetal*, uint32_t> bottomLevelIndices;
	std::vector<MTLAccelerationStructureUserIDInstanceDescriptor> instances(source.size());
	for (size_t i = 0; i < source.size(); ++i) {
		const auto& input = source[i];
		auto blas = std::dynamic_pointer_cast<AccelerationStructureMetal>(input.BottomLevel);
		if (!blas || !blas->belongsTo(driver) || blas->isTopLevel())
			throw std::runtime_error("Metal TLAS instance requires a BLAS from this driver");

		const auto [index, inserted] = bottomLevelIndices.try_emplace(blas.get(), static_cast<uint32_t>(bottomLevels.count));
		if (inserted)
			[bottomLevels addObject:blas->getHandle()];

		auto& instance = instances[i];
		auto transform = input.Transform;
		for (uint8_t column = 0; column < 4; ++column) {
			for (uint8_t row = 0; row < 3; ++row) {
				const float value = transform(row, column);
				if (!std::isfinite(value))
					throw std::runtime_error("Metal ray tracing instance transform must be finite");
				instance.transformationMatrix.columns[column][row] = value;
			}
		}
		instance.options = input.DoubleSided ? MTLAccelerationStructureInstanceOptionDisableTriangleCulling : MTLAccelerationStructureInstanceOptionNone;
		instance.mask = input.Mask;
		instance.intersectionFunctionTableOffset = input.HitGroupOffset;
		instance.accelerationStructureIndex = index->second;
		instance.userID = input.CustomIndex;
	}

	auto* descriptor = [MTLInstanceAccelerationStructureDescriptor descriptor];
	const size_t stride = sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);
	descriptor.instanceDescriptorBuffer = allocateASBuffer(
		driver.getDevice(), std::max<size_t>(1, instances.size()) * stride, MTLResourceStorageModeShared, "Acceleration structure instances");
	if (!instances.empty())
		std::memcpy(descriptor.instanceDescriptorBuffer.contents, instances.data(), instances.size() * stride);
	descriptor.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeUserID;
	descriptor.instanceDescriptorStride = stride;
	descriptor.instanceCount = instances.size();
	descriptor.instancedAccelerationStructures = bottomLevels;
	descriptor.usage = allowUpdate ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone;
	return descriptor;
}

}

std::shared_ptr<VertexBufferInterface> DriverMetal::createRayTracingVertexBuffer(const void* data, size_t count, size_t stride) {
	requireOwnerThread();
	if (!rayTracingCapabilities().AccelerationStructure)
		throw UnsupportedRayTracing("Metal ray tracing vertex buffers are unavailable");
	if (!count || !stride || count > mDevice.maxBufferLength / stride)
		throw std::runtime_error("Invalid Metal ray tracing vertex buffer size");
	auto buffer = std::make_shared<VertexBufferMetal>(count, stride);
	if (!buffer->getBuffer())
		throw std::runtime_error("Metal ray tracing vertex buffer allocation failed");
	if (data)
		buffer->setData(data, count, stride);
	return buffer;
}

std::shared_ptr<AccelerationStructure> DriverMetal::createBottomLevelAS(const std::vector<RayTracingGeometry>& source, bool allowUpdate) {
	requireOwnerThread();
	if (!rayTracingCapabilities().AccelerationStructure)
		throw UnsupportedRayTracing("Metal acceleration structures are unavailable");
	if (source.empty())
		throw std::runtime_error("Metal BLAS requires geometry");

	auto* geometries = [NSMutableArray<MTLAccelerationStructureGeometryDescriptor*> arrayWithCapacity:source.size()];
	for (const auto& input : source) {
		MTLAccelerationStructureGeometryDescriptor* geometry;
		if (input.Aabbs.empty())
			geometry = triangleDescriptor(mDevice, input);
		else
			geometry = boundingBoxDescriptor(mDevice, input);
		geometry.opaque = input.Opaque;
		[geometries addObject:geometry];
	}
	auto* descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor];
	descriptor.geometryDescriptors = geometries;
	descriptor.usage = allowUpdate ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone;

	auto output = std::make_shared<AccelerationStructureMetal>();
	output->mDriver = this;
	output->mAllowUpdate = allowUpdate;
	if (allowUpdate) {
		output->mSource = source;
		output->mDescriptor = descriptor;
	}
	endPass();
	output->mHandle = encodeAccelerationStructure(*this, descriptor);
	return output;
}

void DriverMetal::refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>& source) {
	requireOwnerThread();
	auto structure = std::dynamic_pointer_cast<AccelerationStructureMetal>(source);
	if (!structure || !structure->belongsTo(*this) || structure->mTopLevel || !structure->mAllowUpdate)
		throw std::runtime_error("Metal BLAS refit requires an updateable BLAS from this driver");

	auto* descriptor = (MTLPrimitiveAccelerationStructureDescriptor*)structure->mDescriptor;
	for (size_t i = 0; i < structure->mSource.size(); ++i) {
		const auto& input = structure->mSource[i];
		if (!input.Aabbs.empty())
			continue;
		auto vertices = std::dynamic_pointer_cast<VertexBufferMetal>(input.Vertices);
		auto indices = std::dynamic_pointer_cast<IndexBufferMetal>(input.Indices);
		validateTriangleBuffers(mDevice, input, vertices, indices);
		auto* geometry = (MTLAccelerationStructureTriangleGeometryDescriptor*)descriptor.geometryDescriptors[i];
		if (vertices->getBuffer() != geometry.vertexBuffer || (indices && indices->getBuffer() != geometry.indexBuffer))
			throw std::runtime_error("Metal BLAS refit requires the original input allocations");
	}
	endPass();
	encodeAccelerationStructure(*this, descriptor, structure->mHandle);
}

std::shared_ptr<AccelerationStructure> DriverMetal::createTopLevelAS(const std::vector<RayTracingInstance>& source, bool allowUpdate) {
	requireOwnerThread();
	if (@available(macOS 12.0, iOS 15.0, *)) {
		if (rayTracingCapabilities().AccelerationStructure) {
			auto* descriptor = instanceDescriptor(*this, source, allowUpdate);
			auto output = std::make_shared<AccelerationStructureMetal>();
			output->mDriver = this;
			output->mTopLevel = true;
			output->mAllowUpdate = allowUpdate;
			output->mDescriptor = descriptor;
			endPass();
			output->mHandle = encodeAccelerationStructure(*this, descriptor);
			return output;
		}
	}
	throw UnsupportedRayTracing("Metal acceleration structures are unavailable");
}

void DriverMetal::refitTopLevelAS(const std::shared_ptr<AccelerationStructure>& source, const std::vector<RayTracingInstance>& instances) {
	requireOwnerThread();
	auto structure = std::dynamic_pointer_cast<AccelerationStructureMetal>(source);
	if (!structure || !structure->belongsTo(*this) || !structure->mTopLevel || !structure->mAllowUpdate)
		throw std::runtime_error("Metal TLAS refit requires an updateable TLAS from this driver");
	auto* oldDescriptor = (MTLInstanceAccelerationStructureDescriptor*)structure->mDescriptor;
	if (oldDescriptor.instanceCount != instances.size())
		throw std::runtime_error("Metal TLAS refit requires an unchanged instance count");

	if (@available(macOS 12.0, iOS 15.0, *)) {
		auto* descriptor = instanceDescriptor(*this, instances, true);
		endPass();
		encodeAccelerationStructure(*this, descriptor, structure->mHandle);
		structure->mDescriptor = descriptor;
		return;
	}
	throw UnsupportedRayTracing("Metal acceleration structures are unavailable");
}

}

#endif
