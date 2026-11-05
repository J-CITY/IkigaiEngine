#include "driverVk.h"
#ifdef VULKAN_BACKEND
#include "helpers.h"
#include "spirv_reflect.h"
#include "storageBufferVk.h"
#include "textureVk.h"
#include "uniformBufferVk.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

using namespace IKIGAI::RENDER;
namespace {
void check(VkResult result, const char* operation) {
	if (result != VK_SUCCESS)
		throw std::runtime_error(std::string(operation) + ": VkResult=" + std::to_string(result));
}
uint64_t alignTo(uint64_t value, uint64_t alignment) {
	if (!alignment)
		throw std::runtime_error("Invalid ray tracing alignment");
	return (value + alignment - 1) / alignment * alignment;
}
VkDeviceAddress address(DriverVk& driver, const VmaBuffer& buffer) {
	VkBufferDeviceAddressInfo info { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	info.buffer = buffer.handle();
	return vkGetBufferDeviceAddress(*driver.mDevice, &info);
}
void dependency(VkCommandBuffer cmd, VkPipelineStageFlags2 src, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dst, VkAccessFlags2 dstAccess) {
	VkMemoryBarrier2 barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
	barrier.srcStageMask = src;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dst;
	barrier.dstAccessMask = dstAccess;
	VkDependencyInfo info { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	info.memoryBarrierCount = 1;
	info.pMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(cmd, &info);
}
VkShaderStageFlagBits stageFlag(ShaderType stage) {
	switch (stage) {
	case ShaderType::COMPUTE:
		return VK_SHADER_STAGE_COMPUTE_BIT;
	case ShaderType::RAYGEN:
		return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	case ShaderType::MISS:
		return VK_SHADER_STAGE_MISS_BIT_KHR;
	case ShaderType::CLOSEST_HIT:
		return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	case ShaderType::ANY_HIT:
		return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
	case ShaderType::INTERSECTION:
		return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
	case ShaderType::CALLABLE:
		return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
	default:
		throw std::runtime_error("RT program contains a non-ray-tracing shader stage");
	}
}

struct ASData {
	VkDevice Device = VK_NULL_HANDLE;
	VkAccelerationStructureKHR Handle = VK_NULL_HANDLE;
	VmaBuffer Storage, Scratch, Instances;
	std::vector<VmaBuffer> Inputs;
	std::vector<std::shared_ptr<void>> Owners;
	VkDeviceAddress Address = 0;
	bool TopLevel = false;
	bool AllowUpdate = false;
	std::vector<VkAccelerationStructureGeometryKHR> Geometries;
	std::vector<uint32_t> PrimitiveCounts;
	std::vector<RayTracingGeometry> Source;
	~ASData() {
		if (Handle)
			vkDestroyAccelerationStructureKHR(Device, Handle, nullptr);
	}
};
class ASVk final : public AccelerationStructure {
public:
	DriverVk* Driver;
	std::shared_ptr<ASData> Data;
	ASVk(DriverVk& driver)
		: Driver(&driver)
		, Data(std::make_shared<ASData>()) {
		Data->Device = *driver.mDevice;
	}
	~ASVk() override {
		if (Data && !Driver->mShuttingDown)
			Driver->destroyDeferred(std::shared_ptr<void>(std::move(Data)));
	}
};
struct Binding {
	uint32_t Set = 0, Index = 0, Count = 1;
	VkDescriptorType Type { };
	VkShaderStageFlags Stages = 0;
	std::set<std::string> Names;
	SpvImageFormat ImageFormat = SpvImageFormatUnknown;
};
struct ProgramData {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    std::vector<IKIGAI::PROFILER::GpuAllocationToken> MemoryObjects;
#endif
	VkDevice Device = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
	VkPipelineLayout Layout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSetLayout> Sets;
	std::vector<VkShaderModule> Modules;
	std::vector<Binding> Bindings;
	VmaBuffer SBT;
	std::array<VkStridedDeviceAddressRegionKHR, 4> Regions { };
	uint32_t RaygenCount = 0, PushSize = 0;
	bool Compute = false, RequiresRayQuery = false;
	std::array<uint32_t, 3> LocalSize { 1, 1, 1 };
	~ProgramData() {
		if (Pipeline)
			vkDestroyPipeline(Device, Pipeline, nullptr);
		if (Layout)
			vkDestroyPipelineLayout(Device, Layout, nullptr);
		for (auto layout : Sets)
			vkDestroyDescriptorSetLayout(Device, layout, nullptr);
		for (auto module : Modules)
			vkDestroyShaderModule(Device, module, nullptr);
	}
};
class ProgramVk final : public RayTracingProgram {
public:
	DriverVk* Driver;
	std::shared_ptr<ProgramData> Data;
	ProgramVk(DriverVk& driver)
		: Driver(&driver)
		, Data(std::make_shared<ProgramData>()) {
		Data->Device = *driver.mDevice;
	}
	bool isRayQuery() const override {
		return Data->Compute && Data->RequiresRayQuery;
	}
	bool isCompute() const override {
		return Data->Compute;
	}
	~ProgramVk() override {
		if (Data && !Driver->mShuttingDown)
			Driver->destroyDeferred(std::shared_ptr<void>(std::move(Data)));
	}
};
struct DispatchData {
	VkDevice Device = VK_NULL_HANDLE;
	VkDescriptorPool Pool = VK_NULL_HANDLE;
	RayTracingDispatch Snapshot;
	VkQueryPool Timestamps = VK_NULL_HANDLE;
	double TimestampPeriod = 0;
	bool Compute = false, RayQuery = false;
	~DispatchData() {
		if (Timestamps) {
			uint64_t ticks[2] { };
			if (vkGetQueryPoolResults(Device, Timestamps, 0, 2, sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
				std::cout << "RAY_TRACING_GPU " << (Compute ? (RayQuery ? "query" : "compute") : "trace")
						  << " ms=" << double(ticks[1] - ticks[0]) * TimestampPeriod / 1e6 << "\n";
			vkDestroyQueryPool(Device, Timestamps, nullptr);
		}
		if (Pool)
			vkDestroyDescriptorPool(Device, Pool, nullptr);
	}
};

struct GraphQueueTiming {
	VkDevice Device = VK_NULL_HANDLE;
	VkQueryPool Queries = VK_NULL_HANDLE;
	double NanosecondsPerTick = 0;

	~GraphQueueTiming() {
		if (!Queries) {
			return;
		}
		std::array<uint64_t, 4> ticks {};
		if (vkGetQueryPoolResults(Device, Queries, 0, 4, sizeof(ticks), ticks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
			const auto begin = std::max(ticks[0], ticks[2]);
			const auto end = std::min(ticks[1], ticks[3]);
			const auto milliseconds = [this](uint64_t interval) { return interval * NanosecondsPerTick / 1e6; };
			std::cout << "GRAPH_GPU_QUEUES backend=vulkan compute_ms=" << milliseconds(ticks[1] - ticks[0])
					  << " graphics_ms=" << milliseconds(ticks[3] - ticks[2]) << " overlap_ms=" << milliseconds(end > begin ? end - begin : 0)
					  << '\n';
		}
		vkDestroyQueryPool(Device, Queries, nullptr);
	}
};

std::shared_ptr<ASVk> buildAS(DriverVk& driver, std::vector<VkAccelerationStructureGeometryKHR> geometries, std::vector<uint32_t> primitiveCounts,
	std::shared_ptr<ASVk> output, bool top, bool update = false) {
	if (geometries.empty())
		throw std::runtime_error("BLAS needs at least one geometry");
	driver.deactivateRenderPass();
	VkAccelerationStructureBuildGeometryInfoKHR build { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	build.type = top ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	auto& data = *output->Data;
	if (data.AllowUpdate)
		build.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	if (update && !data.AllowUpdate)
		throw std::runtime_error("AS was built without allowUpdate");
	if (update && (data.TopLevel != top || primitiveCounts != data.PrimitiveCounts))
		throw std::runtime_error("AS refit requires unchanged topology");
	build.mode = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = static_cast<uint32_t>(geometries.size());
	build.pGeometries = geometries.data();
	VkAccelerationStructureBuildSizesInfoKHR sizes { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	vkGetAccelerationStructureBuildSizesKHR(*driver.mDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, primitiveCounts.data(), &sizes);
	if (!update) {
		data.TopLevel = top;
		data.Storage = UtilityVk::CreateBuffer(std::max<uint64_t>(sizes.accelerationStructureSize, 256),
			vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress);
		const auto alignment = driver.mASProperties.minAccelerationStructureScratchOffsetAlignment;
		data.Scratch = UtilityVk::CreateBuffer(std::max<uint64_t>({ sizes.buildScratchSize, sizes.updateScratchSize, 1 }) + alignment,
			vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
		VkAccelerationStructureCreateInfoKHR create { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		create.buffer = data.Storage.handle();
		create.size = sizes.accelerationStructureSize;
		create.type = build.type;
		check(vkCreateAccelerationStructureKHR(data.Device, &create, nullptr, &data.Handle), "vkCreateAccelerationStructureKHR");
	}
	const auto alignment = driver.mASProperties.minAccelerationStructureScratchOffsetAlignment;
	build.srcAccelerationStructure = update ? data.Handle : VK_NULL_HANDLE;
	build.dstAccelerationStructure = data.Handle;
	build.scratchData.deviceAddress = alignTo(address(driver, data.Scratch), alignment);
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(primitiveCounts.size());
	for (size_t i = 0; i < ranges.size(); ++i)
		ranges[i].primitiveCount = primitiveCounts[i];
	const auto* rangePtr = ranges.data();
	const VkCommandBuffer cmd = *driver.getCurrentFrame().mCommandBuffer;
	dependency(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
	vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rangePtr);
	dependency(cmd, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
	VkAccelerationStructureDeviceAddressInfoKHR addressInfo { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
	addressInfo.accelerationStructure = data.Handle;
	data.Address = vkGetAccelerationStructureDeviceAddressKHR(data.Device, &addressInfo);
	data.Geometries = std::move(geometries);
	data.PrimitiveCounts = std::move(primitiveCounts);
	driver.mRayTracingFrameResources.push_back(output);
	return output;
}
}

void DriverVk::configureRayTracing(std::vector<const char*>& extensions) {
	const auto available = mPhysicalDevice.enumerateDeviceExtensionProperties();
	auto has = [&](const char* name) {
		return std::any_of(available.begin(), available.end(), [&](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
	};
	if (std::getenv("IKIGAI_DISABLE_RT") || !has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
		|| !has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
		std::cout << "RAY_TRACING_CAPABILITIES AS=0 query=0 pipeline=0\n";
		return;
	}
	VkPhysicalDeviceFeatures2 features { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	mRTCoreFeatures.pNext = &mASFeatures;
	void** next = &mASFeatures.pNext;
	if (has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)) {
		*next = &mRTFeatures;
		next = &mRTFeatures.pNext;
	}
	if (has(VK_KHR_RAY_QUERY_EXTENSION_NAME)) {
		*next = &mRQFeatures;
	}
	features.pNext = &mRTCoreFeatures;
	vkGetPhysicalDeviceFeatures2(*mPhysicalDevice, &features);
	const auto core = mRTCoreFeatures;
	const auto as = mASFeatures;
	const auto rt = mRTFeatures;
	const auto rq = mRQFeatures;
	// Query chains and enabled chains are deliberately separate; do not enable
	// unrelated supported features just because getFeatures2 returned them.
	mRTCoreFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	mASFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	mRTFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
	mRQFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	if (!core.bufferDeviceAddress || !as.accelerationStructure)
		return;
	mRayTracingCapabilities.AccelerationStructure = true;
	mRayTracingCapabilities.Pipeline = rt.rayTracingPipeline;
	mRayTracingCapabilities.RayQuery = rq.rayQuery && !std::getenv("IKIGAI_DISABLE_RAY_QUERY");
	mRayTracingCapabilities.DescriptorIndexing = core.shaderSampledImageArrayNonUniformIndexing;
	mRTCoreFeatures.bufferDeviceAddress = VK_TRUE;
	mRTCoreFeatures.scalarBlockLayout = core.scalarBlockLayout;
	mRTCoreFeatures.runtimeDescriptorArray = core.runtimeDescriptorArray;
	mRTCoreFeatures.shaderSampledImageArrayNonUniformIndexing = core.shaderSampledImageArrayNonUniformIndexing;
	mRTCoreFeatures.shaderStorageBufferArrayNonUniformIndexing = core.shaderStorageBufferArrayNonUniformIndexing;
	mRTCoreFeatures.shaderStorageImageArrayNonUniformIndexing = core.shaderStorageImageArrayNonUniformIndexing;
	mRTCoreFeatures.pNext = &mASFeatures;
	mASFeatures.accelerationStructure = VK_TRUE;
	next = &mASFeatures.pNext;
	extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
	extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	if (mRayTracingCapabilities.Pipeline) {
		mRTFeatures.rayTracingPipeline = VK_TRUE;
		*next = &mRTFeatures;
		next = &mRTFeatures.pNext;
		extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
	}
	if (mRayTracingCapabilities.RayQuery) {
		mRQFeatures.rayQuery = VK_TRUE;
		*next = &mRQFeatures;
		extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	}
	VkPhysicalDeviceProperties2 properties { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	properties.pNext = &mASProperties;
	if (mRayTracingCapabilities.Pipeline)
		mASProperties.pNext = &mRTProperties;
	vkGetPhysicalDeviceProperties2(*mPhysicalDevice, &properties);
	mRayTracingCapabilities.MaxRecursionDepth = mRTProperties.maxRayRecursionDepth;
	mRayTracingCapabilities.MaxDispatchInvocations = mRTProperties.maxRayDispatchInvocationCount;
	std::cout << "RAY_TRACING_CAPABILITIES AS=1 query=" << mRayTracingCapabilities.RayQuery << " pipeline=" << mRayTracingCapabilities.Pipeline
			  << " recursion=" << mRayTracingCapabilities.MaxRecursionDepth << "\n";
}

std::shared_ptr<VertexBufferInterface> DriverVk::createRayTracingVertexBuffer(const void* data, size_t count, size_t stride) {
	return std::make_shared<VertexBufferVk>(const_cast<void*>(data), count, stride);
}

std::shared_ptr<AccelerationStructure> DriverVk::createBottomLevelAS(const std::vector<RayTracingGeometry>& source, bool allowUpdate) {
	if (!mRayTracingCapabilities.AccelerationStructure)
		throw std::runtime_error("Vulkan GPU does not support acceleration structures");
	auto output = std::make_shared<ASVk>(*this);
	output->Data->AllowUpdate = allowUpdate;
	output->Data->Source = source;
	std::vector<VkAccelerationStructureGeometryKHR> geometries;
	std::vector<uint32_t> counts;
	for (const auto& input : source) {
		VkAccelerationStructureGeometryKHR geometry { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		geometry.flags = input.Opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
		if (!input.Aabbs.empty()) {
			for (const auto& box : input.Aabbs)
				for (size_t a = 0; a < 3; ++a)
					if (!std::isfinite(box[a]) || !std::isfinite(box[a + 3]) || box[a] > box[a + 3])
						throw std::runtime_error("Invalid RT AABB");
			output->Data->Inputs.push_back(UtilityVk::CreateBuffer(input.Aabbs.size() * sizeof(input.Aabbs[0]),
				vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
				UtilityVk::MemoryUsage::Staging));
			auto& buffer = output->Data->Inputs.back();
			buffer.upload(input.Aabbs.data(), input.Aabbs.size() * sizeof(input.Aabbs[0]));
			geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
			geometry.geometry.aabbs = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR };
			geometry.geometry.aabbs.data.deviceAddress = address(*this, buffer);
			geometry.geometry.aabbs.stride = sizeof(input.Aabbs[0]);
			counts.push_back(static_cast<uint32_t>(input.Aabbs.size()));
		}
		else {
			auto vertices = std::dynamic_pointer_cast<VertexBufferVk>(input.Vertices);
			auto indices = std::dynamic_pointer_cast<IndexBufferVk>(input.Indices);
			if (!vertices || !input.VertexCount || input.VertexStride < 12)
				throw std::runtime_error("Invalid RT triangle vertex input");
			const auto bytes = vertices->getSizeByte();
			const uint64_t required = uint64_t(input.VertexCount - 1) * input.VertexStride + 12;
			if (input.VertexOffset % 4 || input.VertexStride % 4 || input.VertexOffset > bytes || required > bytes - input.VertexOffset
				|| vertices->getBuffer().allocator() != mAllocator)
				throw std::runtime_error("RT vertex range/alignment/driver mismatch");
			const uint32_t n = indices ? input.IndexCount : input.VertexCount;
			if (!n || n % 3 || (indices && input.IndexStride != 2 && input.IndexStride != 4))
				throw std::runtime_error("Invalid RT triangle/index count");
			if (n / 3 > mASProperties.maxPrimitiveCount)
				throw std::runtime_error("RT primitive count exceeds device limit");
			if (indices
				&& (input.IndexOffset % input.IndexStride || input.IndexOffset > indices->getSizeByte()
					|| uint64_t(n) * input.IndexStride > indices->getSizeByte() - input.IndexOffset
					|| indices->getBuffer().allocator() != mAllocator))
				throw std::runtime_error("RT index range/alignment/driver mismatch");
			geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			auto& triangles = geometry.geometry.triangles;
			triangles = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
			triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			triangles.vertexStride = input.VertexStride;
			triangles.vertexData.deviceAddress = address(*this, vertices->getBuffer()) + input.VertexOffset;
			triangles.maxVertex = input.VertexCount - 1;
			triangles.indexType = indices ? (input.IndexStride == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32) : VK_INDEX_TYPE_NONE_KHR;
			if (indices)
				triangles.indexData.deviceAddress = address(*this, indices->getBuffer()) + input.IndexOffset;
			output->Data->Owners.push_back(vertices);
			if (indices)
				output->Data->Owners.push_back(indices);
			counts.push_back(n / 3);
		}
		geometries.push_back(geometry);
	}
	if (geometries.size() > mASProperties.maxGeometryCount)
		throw std::runtime_error("RT geometry count exceeds device limit");
	return buildAS(*this, std::move(geometries), std::move(counts), output, false);
}

namespace {
std::vector<VkAccelerationStructureInstanceKHR> packInstances(
	DriverVk& driver, const std::vector<RayTracingInstance>& source, std::vector<std::shared_ptr<void>>& owners) {
	std::vector<VkAccelerationStructureInstanceKHR> instances;
	for (const auto& input : source) {
		auto blas = std::dynamic_pointer_cast<ASVk>(input.BottomLevel);
		if (!blas || blas->Driver != &driver || blas->Data->TopLevel)
			throw std::runtime_error("TLAS instance requires a BLAS from this driver");
		if (input.CustomIndex > 0xffffff || input.HitGroupOffset > 0xffffff)
			throw std::runtime_error("RT instance 24-bit index overflow");
		VkAccelerationStructureInstanceKHR instance { };
		auto transform = input.Transform;
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 4; ++c) {
				if (!std::isfinite(transform(r, c)))
					throw std::runtime_error("RT instance transform is not finite");
				instance.transform.matrix[r][c] = transform(r, c);
			}
		instance.instanceCustomIndex = input.CustomIndex;
		instance.mask = input.Mask;
		instance.instanceShaderBindingTableRecordOffset = input.HitGroupOffset;
		instance.flags = input.DoubleSided ? VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR : 0;
		instance.accelerationStructureReference = blas->Data->Address;
		instances.push_back(instance);
		owners.push_back(blas);
	}
	return instances;
}
}

void DriverVk::refitBottomLevelAS(const std::shared_ptr<AccelerationStructure>& structure) {
	auto output = std::dynamic_pointer_cast<ASVk>(structure);
	if (!output || output->Driver != this || output->Data->TopLevel)
		throw std::runtime_error("BLAS refit requires a BLAS from this driver");
	auto& data = *output->Data;
	for (size_t i = 0; i < data.Source.size(); ++i) {
		const auto& input = data.Source[i];
		if (!input.Aabbs.empty())
			continue;
		auto vertices = std::dynamic_pointer_cast<VertexBufferVk>(input.Vertices);
		auto indices = std::dynamic_pointer_cast<IndexBufferVk>(input.Indices);
		const auto& geometry = data.Geometries[i].geometry.triangles;
		if (address(*this, vertices->getBuffer()) + input.VertexOffset != geometry.vertexData.deviceAddress
			|| (indices && address(*this, indices->getBuffer()) + input.IndexOffset != geometry.indexData.deviceAddress))
			throw std::runtime_error("BLAS input allocation changed; rebuild instead of refit");
	}
	buildAS(*this, data.Geometries, data.PrimitiveCounts, output, false, true);
}

void DriverVk::refitTopLevelAS(const std::shared_ptr<AccelerationStructure>& structure, const std::vector<RayTracingInstance>& source) {
	auto output = std::dynamic_pointer_cast<ASVk>(structure);
	if (!output || output->Driver != this || !output->Data->TopLevel || !output->Data->AllowUpdate)
		throw std::runtime_error("TLAS refit requires an updateable TLAS from this driver");
	auto& data = *output->Data;
	if (data.PrimitiveCounts != std::vector<uint32_t> { static_cast<uint32_t>(source.size()) })
		throw std::runtime_error("TLAS refit requires unchanged instance count");
	std::vector<std::shared_ptr<void>> owners;
	auto instances = packInstances(*this, source, owners);
	// Keep previous inputs alive until commands already recorded have finished.
	destroyDeferred(std::move(data.Instances));
	mRayTracingFrameResources.insert(mRayTracingFrameResources.end(), data.Owners.begin(), data.Owners.end());
	data.Owners = std::move(owners);
	data.Instances = UtilityVk::CreateBuffer(std::max<size_t>(1, instances.size()) * sizeof(VkAccelerationStructureInstanceKHR),
		vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
		UtilityVk::MemoryUsage::Staging);
	if (!instances.empty())
		data.Instances.upload(instances.data(), instances.size() * sizeof(instances[0]));
	auto geometry = data.Geometries;
	geometry[0].geometry.instances.data.deviceAddress = address(*this, data.Instances);
	buildAS(*this, std::move(geometry), data.PrimitiveCounts, output, true, true);
}

std::shared_ptr<AccelerationStructure> DriverVk::createTopLevelAS(const std::vector<RayTracingInstance>& source, bool allowUpdate) {
	if (!mRayTracingCapabilities.AccelerationStructure)
		throw std::runtime_error("Vulkan GPU does not support acceleration structures");
	if (source.size() > mASProperties.maxInstanceCount)
		throw std::runtime_error("RT instance count exceeds device limit");
	auto output = std::make_shared<ASVk>(*this);
	output->Data->AllowUpdate = allowUpdate;
	auto instances = packInstances(*this, source, output->Data->Owners);
	output->Data->Instances = UtilityVk::CreateBuffer(std::max<size_t>(1, instances.size()) * sizeof(VkAccelerationStructureInstanceKHR),
		vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
		UtilityVk::MemoryUsage::Staging);
	if (!instances.empty())
		output->Data->Instances.upload(instances.data(), instances.size() * sizeof(instances[0]));
	VkAccelerationStructureGeometryKHR geometry { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
	geometry.geometry.instances.data.deviceAddress = address(*this, output->Data->Instances);
	return buildAS(*this, { geometry }, { static_cast<uint32_t>(instances.size()) }, output, true);
}

std::shared_ptr<RayTracingProgram> DriverVk::createRayTracingProgram(const RayTracingProgramResource& resource) {
	return createRayTracingProgramImpl(resource, true);
}
std::shared_ptr<RayTracingProgram> DriverVk::createComputeProgram(const RayTracingProgramResource& resource) {
	if (resource.Shaders.size() != 1 || resource.Shaders[0].Stage != ShaderType::COMPUTE || !resource.Groups.empty())
		throw std::runtime_error("Compute program needs one compute shader and no SBT groups");
	return createRayTracingProgramImpl(resource, false);
}
std::shared_ptr<RayTracingProgram> DriverVk::createRayTracingProgramImpl(const RayTracingProgramResource& resource, bool requireRayQuery) {
	const bool compute = resource.Shaders.size() == 1 && resource.Shaders[0].Stage == ShaderType::COMPUTE;
	if (compute && requireRayQuery && !mRayTracingCapabilities.RayQuery)
		throw UnsupportedRayTracing("Vulkan GPU does not support ray queries");
	if (!compute && !mRayTracingCapabilities.Pipeline)
		throw UnsupportedRayTracing("Vulkan GPU does not support ray tracing pipelines");
	if (resource.RequiresDescriptorIndexing && !mRayTracingCapabilities.DescriptorIndexing)
		throw UnsupportedRayTracing("RT program requires sampled-image descriptor indexing");
	if (!compute && (!resource.MaxRecursionDepth || resource.MaxRecursionDepth > mRTProperties.maxRayRecursionDepth))
		throw std::runtime_error("Unsupported RT recursion depth");
	nlohmann::json identity;
	identity["recursion"] = resource.MaxRecursionDepth;
	identity["push"] = resource.PushConstantSize;
	identity["rayQuery"] = requireRayQuery;
	for (const auto& shader : resource.Shaders)
		identity["shaders"].push_back({ shader.Name, static_cast<int>(shader.Stage), shader.Source, shader.EntryPoint });
	for (const auto& group : resource.Groups)
		identity["groups"].push_back({ group.Name, group.General, group.ClosestHit, group.AnyHit, group.Intersection, group.RecordData });
	const auto key = identity.dump();
	for (auto i = mRayTracingPrograms.begin(); i != mRayTracingPrograms.end();)
		if (i->second.expired())
			i = mRayTracingPrograms.erase(i);
		else
			++i;
	if (auto cached = mRayTracingPrograms.find(key); cached != mRayTracingPrograms.end())
		if (auto program = cached->second.lock())
			return program;
	auto output = std::make_shared<ProgramVk>(*this);
	auto& data = *output->Data;
	data.PushSize = resource.PushConstantSize;
	data.Compute = compute;
	data.RequiresRayQuery = compute && requireRayQuery;
	const auto limits = mPhysicalDevice.getProperties().limits;
	if (data.PushSize % 4 || data.PushSize > limits.maxPushConstantsSize)
		throw std::runtime_error("Invalid RT push constant size");
	std::map<std::string, uint32_t> shaderIndices;
	std::vector<VkPipelineShaderStageCreateInfo> stages;
	for (const auto& shader : resource.Shaders) {
		if (!compute && shader.Stage == ShaderType::COMPUTE)
			throw std::runtime_error("Cannot mix compute and ray tracing stages in one program");
		if (shader.Name.empty() || !shaderIndices.emplace(shader.Name, static_cast<uint32_t>(stages.size())).second)
			throw std::runtime_error("Duplicate RT shader name");
		if (shader.EntryPoint != "main")
			throw std::runtime_error("GLSL RT program entry point must be main");
		const auto flag = stageFlag(shader.Stage);
		const auto code = CompileGlslToSpirv(shader.Stage, shader.Source, { });
		VkShaderModuleCreateInfo moduleInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		moduleInfo.codeSize = code.size() * 4;
		moduleInfo.pCode = code.data();
		VkShaderModule module;
		check(vkCreateShaderModule(data.Device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
		data.Modules.push_back(module);
#if IKIGAI_ENABLE_MEMORY_TRACKING
        try { data.MemoryObjects.push_back(IKIGAI::PROFILER::TrackGpuObject(IKIGAI::PROFILER::GpuBackend::Vulkan,
            reinterpret_cast<std::uintptr_t>(data.Device), IKIGAI::PROFILER::GpuHandleValue(module),
            IKIGAI::PROFILER::GpuResourceKind::Shader, "Ray shader module")); } catch (...) { IKIGAI::PROFILER::GpuMemoryProfiler::instance().noteDroppedRecord(); }
#endif
		VkPipelineShaderStageCreateInfo stage { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stage.stage = flag;
		stage.module = module;
		stage.pName = shader.EntryPoint.c_str();
		stages.push_back(stage);
		SpvReflectShaderModule reflected { };
		if (spvReflectCreateShaderModule(code.size() * 4, code.data(), &reflected) != SPV_REFLECT_RESULT_SUCCESS)
			throw std::runtime_error("RT SPIR-V reflection failed");
		struct Release {
			SpvReflectShaderModule& M;
			~Release() {
				spvReflectDestroyShaderModule(&M);
			}
		} release { reflected };
		if (compute && reflected.entry_point_count) {
			const auto local = reflected.entry_points[0].local_size;
			data.LocalSize = { local.x, local.y, local.z };
			if (!local.x || !local.y || !local.z)
				throw std::runtime_error("Ray query program needs a fixed nonzero local size");
		}
		for (uint32_t i = 0; i < reflected.descriptor_binding_count; ++i) {
			const auto& b = reflected.descriptor_bindings[i];
			if (!b.count)
				throw std::runtime_error("Runtime descriptor arrays need an explicit bounded layout; use fixed-size arrays in RT programs");
			auto found
				= std::find_if(data.Bindings.begin(), data.Bindings.end(), [&](const auto& v) { return v.Set == b.set && v.Index == b.binding; });
			const auto type = static_cast<VkDescriptorType>(b.descriptor_type);
			if (type != VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR && type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
				&& type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
				&& type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				throw std::runtime_error("Unsupported RT descriptor type");
			if (found == data.Bindings.end())
				data.Bindings.push_back(
					{ b.set, b.binding, b.count, type, static_cast<VkShaderStageFlags>(flag), { b.name ? b.name : "" }, b.image.image_format });
			else {
				if (found->Count != b.count || found->Type != type)
					throw std::runtime_error("Incompatible RT descriptor at set/binding");
				if (found->ImageFormat != SpvImageFormatUnknown && b.image.image_format != SpvImageFormatUnknown
					&& found->ImageFormat != b.image.image_format)
					throw std::runtime_error("Incompatible RT storage image qualifiers at set/binding");
				if (found->ImageFormat == SpvImageFormatUnknown)
					found->ImageFormat = b.image.image_format;
				found->Stages |= flag;
				found->Names.insert(b.name ? b.name : "");
			}
		}
		for (uint32_t i = 0; i < reflected.push_constant_block_count; ++i) {
			const auto& b = reflected.push_constant_blocks[i];
			if (b.offset + b.size > data.PushSize)
				throw std::runtime_error("RT program PushConstantSize is smaller than reflected block");
		}
	}
	uint32_t setCount = 0;
	for (const auto& b : data.Bindings)
		setCount = std::max(setCount, b.Set + 1);
	if (setCount > limits.maxBoundDescriptorSets)
		throw std::runtime_error("RT descriptor set index exceeds device limit");
	for (uint32_t set = 0; set < setCount; ++set) {
		std::vector<VkDescriptorSetLayoutBinding> bindings;
		for (const auto& b : data.Bindings)
			if (b.Set == set)
				bindings.push_back({ b.Index, b.Type, b.Count, b.Stages, nullptr });
		VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		info.bindingCount = static_cast<uint32_t>(bindings.size());
		info.pBindings = bindings.data();
		VkDescriptorSetLayout layout;
		check(vkCreateDescriptorSetLayout(data.Device, &info, nullptr, &layout), "vkCreateDescriptorSetLayout");
		data.Sets.push_back(layout);
	}
	VkPushConstantRange push { VK_SHADER_STAGE_ALL, 0, data.PushSize };
	VkPipelineLayoutCreateInfo layout { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layout.setLayoutCount = static_cast<uint32_t>(data.Sets.size());
	layout.pSetLayouts = data.Sets.data();
	if (data.PushSize) {
		layout.pushConstantRangeCount = 1;
		layout.pPushConstantRanges = &push;
	}
	check(vkCreatePipelineLayout(data.Device, &layout, nullptr, &data.Layout), "vkCreatePipelineLayout");
	if (compute) {
		if (!resource.Groups.empty())
			throw std::runtime_error("Compute ray query programs do not use SBT groups");
		VkComputePipelineCreateInfo pipeline { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		pipeline.stage = stages.front();
		pipeline.layout = data.Layout;
		check(vkCreateComputePipelines(data.Device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &data.Pipeline), "vkCreateComputePipelines(RayQuery)");
#if IKIGAI_ENABLE_MEMORY_TRACKING
        try { data.MemoryObjects.push_back(IKIGAI::PROFILER::TrackGpuObject(IKIGAI::PROFILER::GpuBackend::Vulkan,
            reinterpret_cast<std::uintptr_t>(data.Device), IKIGAI::PROFILER::GpuHandleValue(data.Pipeline),
            IKIGAI::PROFILER::GpuResourceKind::Pipeline, "Ray-query pipeline")); } catch (...) { IKIGAI::PROFILER::GpuMemoryProfiler::instance().noteDroppedRecord(); }
#endif
		mRayTracingPrograms[key] = output;
		return output;
	}
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
	std::array<std::vector<uint32_t>, 4> sections;
	std::set<std::string> groupNames;
	auto index = [&](const std::string& name, ShaderType expected) {
		if (name.empty())
			return uint32_t(VK_SHADER_UNUSED_KHR);
		auto i = shaderIndices.at(name);
		if (resource.Shaders[i].Stage != expected)
			throw std::runtime_error("RT group has incompatible shader stage: " + name);
		return i;
	};
	for (const auto& group : resource.Groups) {
		if (group.Name.empty() || !groupNames.insert(group.Name).second)
			throw std::runtime_error("Duplicate RT group name");
		VkRayTracingShaderGroupCreateInfoKHR g { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
		g.generalShader = g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
		size_t section = 2;
		if (!group.General.empty()) {
			if (!group.ClosestHit.empty() || !group.AnyHit.empty() || !group.Intersection.empty())
				throw std::runtime_error("RT general group cannot contain hit shaders");
			g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			g.generalShader = shaderIndices.at(group.General);
			const auto type = resource.Shaders[g.generalShader].Stage;
			if (type == ShaderType::RAYGEN)
				section = 0;
			else if (type == ShaderType::MISS)
				section = 1;
			else if (type == ShaderType::CALLABLE)
				section = 3;
			else
				throw std::runtime_error("Invalid RT general group");
		}
		else {
			g.type = group.Intersection.empty() ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
												: VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
			g.closestHitShader = index(group.ClosestHit, ShaderType::CLOSEST_HIT);
			g.anyHitShader = index(group.AnyHit, ShaderType::ANY_HIT);
			g.intersectionShader = index(group.Intersection, ShaderType::INTERSECTION);
			if (g.closestHitShader == VK_SHADER_UNUSED_KHR && g.anyHitShader == VK_SHADER_UNUSED_KHR && g.intersectionShader == VK_SHADER_UNUSED_KHR)
				throw std::runtime_error("Empty RT hit group");
		}
		sections[section].push_back(static_cast<uint32_t>(groups.size()));
		groups.push_back(g);
	}
	if (sections[0].empty())
		throw std::runtime_error("RT program requires at least one raygen group");
	VkRayTracingPipelineCreateInfoKHR pipeline { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
	pipeline.stageCount = static_cast<uint32_t>(stages.size());
	pipeline.pStages = stages.data();
	pipeline.groupCount = static_cast<uint32_t>(groups.size());
	pipeline.pGroups = groups.data();
	pipeline.maxPipelineRayRecursionDepth = resource.MaxRecursionDepth;
	pipeline.layout = data.Layout;
	check(vkCreateRayTracingPipelinesKHR(data.Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline, nullptr, &data.Pipeline),
		"vkCreateRayTracingPipelinesKHR");
#if IKIGAI_ENABLE_MEMORY_TRACKING
        try { data.MemoryObjects.push_back(IKIGAI::PROFILER::TrackGpuObject(IKIGAI::PROFILER::GpuBackend::Vulkan,
            reinterpret_cast<std::uintptr_t>(data.Device), IKIGAI::PROFILER::GpuHandleValue(data.Pipeline),
            IKIGAI::PROFILER::GpuResourceKind::Pipeline, "Ray-tracing pipeline")); } catch (...) { IKIGAI::PROFILER::GpuMemoryProfiler::instance().noteDroppedRecord(); }
#endif
	const uint64_t handleSize = mRTProperties.shaderGroupHandleSize, handleAlign = mRTProperties.shaderGroupHandleAlignment,
				   baseAlign = mRTProperties.shaderGroupBaseAlignment;
	std::vector<uint8_t> handles(groups.size() * handleSize);
	check(vkGetRayTracingShaderGroupHandlesKHR(data.Device, data.Pipeline, 0, static_cast<uint32_t>(groups.size()), handles.size(), handles.data()),
		"vkGetRayTracingShaderGroupHandlesKHR");
	std::array<uint64_t, 4> offsets { }, strides { };
	uint64_t bytes = 0;
	for (size_t s = 0; s < 4; ++s) {
		uint64_t recordSize = handleSize;
		for (auto i : sections[s])
			recordSize = std::max<uint64_t>(recordSize, handleSize + resource.Groups[i].RecordData.size());
		strides[s] = alignTo(recordSize, s == 0 ? baseAlign : handleAlign);
		if (strides[s] > mRTProperties.maxShaderGroupStride)
			throw std::runtime_error("RT SBT record exceeds maxShaderGroupStride");
		offsets[s] = alignTo(bytes, baseAlign);
		bytes = offsets[s] + sections[s].size() * strides[s];
	}
	data.SBT = UtilityVk::CreateBuffer(bytes + baseAlign,
		vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, UtilityVk::MemoryUsage::Staging);
	const auto rawAddress = address(*this, data.SBT);
	const auto aligned = alignTo(rawAddress, baseAlign);
	const auto padding = aligned - rawAddress;
	std::vector<uint8_t> contents(bytes + padding, 0);
	for (size_t s = 0; s < 4; ++s)
		for (size_t r = 0; r < sections[s].size(); ++r) {
			const auto i = sections[s][r];
			const auto offset = padding + offsets[s] + r * strides[s];
			std::memcpy(contents.data() + offset, handles.data() + i * handleSize, handleSize);
			const auto& extra = resource.Groups[i].RecordData;
			if (!extra.empty())
				std::memcpy(contents.data() + offset + handleSize, extra.data(), extra.size());
		}
	data.SBT.upload(contents.data(), contents.size());
	for (size_t s = 0; s < 4; ++s)
		if (!sections[s].empty())
			data.Regions[s] = { aligned + offsets[s], strides[s], sections[s].size() * strides[s] };
	data.RaygenCount = static_cast<uint32_t>(sections[0].size());
	mRayTracingPrograms[key] = output;
	return output;
}

void DriverVk::traceRays(const RayTracingDispatch& dispatch) {
	executeRayProgram(dispatch, false);
}
void DriverVk::dispatchRayQueries(const RayTracingDispatch& dispatch) {
	if (!dispatch.Program || !dispatch.Program->isRayQuery())
		throw std::runtime_error("Ray query dispatch requires a ray query program");
	executeRayProgram(dispatch, true);
}
void DriverVk::dispatchComputeProgram(const RayTracingDispatch& dispatch) {
	executeRayProgram(dispatch, true);
}

void DriverVk::dispatchGraphCompute(const RayTracingDispatch& dispatch) {
	requireOwnerThread();
	if (!hasGraphComputeQueue() || mGraphComputePending) {
		throw std::logic_error("Vulkan graph compute queue is unavailable");
	}
	mGraphComputeBuffers.clear();
	for (const auto& [binding, resources] : dispatch.Bindings) {
		for (const auto& resource : resources) {
			const auto* storage = std::get_if<std::shared_ptr<StorageBufferInterface>>(&resource);
			auto buffer = storage ? std::dynamic_pointer_cast<StorageBufferVk>(*storage) : nullptr;
			if (!buffer || !buffer->getBuffer()) {
				throw std::invalid_argument("Vulkan graph compute requires storage buffer bindings");
			}
			const auto native = buffer->getBuffer().handle();
			if (std::find(mGraphComputeBuffers.begin(), mGraphComputeBuffers.end(), native) == mGraphComputeBuffers.end()) {
				mGraphComputeBuffers.push_back(native);
			}
		}
	}
	auto& frame = getCurrentFrame();
	auto lists = mDevice.allocateCommandBuffers(vk::CommandBufferAllocateInfo(*mGraphComputePool, vk::CommandBufferLevel::ePrimary, 1));
	auto& list = lists.front();
	list.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
	transferGraphBuffers(*list, mQueueFamilyIndex, mGraphComputeFamily, false);
	std::shared_ptr<GraphQueueTiming> timing;
	if (mProfileGraphQueues) {
		timing = std::make_shared<GraphQueueTiming>();
		timing->Device = *mDevice;
		timing->NanosecondsPerTick = mPhysicalDevice.getProperties().limits.timestampPeriod;
		const VkQueryPoolCreateInfo queryInfo {
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 4
		};
		check(vkCreateQueryPool(*mDevice, &queryInfo, nullptr, &timing->Queries), "Create graph queue timestamps");
		mRayTracingFrameResources.push_back(timing);
		vkCmdResetQueryPool(*list, timing->Queries, 0, 2);
		vkCmdWriteTimestamp2(*list, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timing->Queries, 0);
	}
	executeRayProgram(dispatch, true, *list);
	if (timing) {
		vkCmdWriteTimestamp2(*list, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, timing->Queries, 1);
	}
	transferGraphBuffers(*list, mGraphComputeFamily, mQueueFamilyIndex, true);
	list.end();

	const auto value = ++mGraphComputeValue;
	frame.TransferLists.reserve(frame.TransferLists.size() + 2);
	deactivateRenderPass();
	transferGraphBuffers(*frame.mCommandBuffer, mQueueFamilyIndex, mGraphComputeFamily, true);
	splitGraphGraphics(value);
	const auto command = *list;
	const auto wait = *mGraphGraphicsTimeline;
	const auto signal = *mGraphComputeTimeline;
	const vk::PipelineStageFlags stage = vk::PipelineStageFlagBits::eAllCommands;
	const vk::TimelineSemaphoreSubmitInfo timeline(1, &value, 1, &value);
	vk::SubmitInfo submit(1, &wait, &stage, 1, &command, 1, &signal);
	submit.setPNext(&timeline);
	mGraphComputeQueue.submit(submit);
	frame.TransferLists.push_back(std::move(list));
	mGraphComputePending = value;
	if (timing) {
		mGraphTiming = timing->Queries;
		vkCmdResetQueryPool(*frame.mCommandBuffer, mGraphTiming, 2, 2);
		vkCmdWriteTimestamp2(*frame.mCommandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, mGraphTiming, 2);
	}
}

void DriverVk::executeRayProgram(const RayTracingDispatch& dispatch, bool rayQuery, VkCommandBuffer computeList) {
	auto program = std::dynamic_pointer_cast<ProgramVk>(dispatch.Program);
	if (!program || program->Driver != this)
		throw std::runtime_error("RT dispatch requires a program from this driver");
	const auto& data = *program->Data;
	if (data.Compute != rayQuery)
		throw std::runtime_error("RT program kind disagrees with dispatch type");
	std::array<uint32_t, 3> groups { };
	const auto limits = mPhysicalDevice.getProperties().limits;
	uint64_t invocations = 1;
	for (size_t i = 0; i < 3; ++i) {
		const auto n = dispatch.Extent[i];
		if (!n)
			throw std::runtime_error("Invalid RT launch extent");
		if (!data.Compute) {
			if (invocations > mRTProperties.maxRayDispatchInvocationCount / n)
				throw std::runtime_error("RT launch exceeds device limit");
			invocations *= n;
		}
		else {
			groups[i] = static_cast<uint32_t>((uint64_t(n) + data.LocalSize[i] - 1) / data.LocalSize[i]);
			if (groups[i] > limits.maxComputeWorkGroupCount[i])
				throw std::runtime_error("Ray query launch exceeds device limit");
		}
	}
	if ((!data.Compute && dispatch.RaygenRecord >= data.RaygenCount) || dispatch.PushConstants.size() != data.PushSize)
		throw std::runtime_error("Invalid RT raygen record or push constant size");
	auto frame = std::make_shared<DispatchData>();
	frame->Device = *mDevice;
	frame->Snapshot = dispatch;
	if (std::getenv("IKIGAI_PROFILE_RT") && mPhysicalDevice.getQueueFamilyProperties().at(mQueueFamilyIndex).timestampValidBits) {
		VkQueryPoolCreateInfo info { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = 2;
		check(vkCreateQueryPool(*mDevice, &info, nullptr, &frame->Timestamps), "vkCreateQueryPool(RT)");
		frame->TimestampPeriod = limits.timestampPeriod;
		frame->Compute = data.Compute;
		frame->RayQuery = data.RequiresRayQuery;
	}
	std::map<VkDescriptorType, uint32_t> counts;
	for (const auto& b : data.Bindings)
		counts[b.Type] += b.Count;
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (auto [type, count] : counts)
		poolSizes.push_back({ type, count });
	std::vector<VkDescriptorSet> sets(data.Sets.size());
	if (!sets.empty()) {
		VkDescriptorPoolCreateInfo pool { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pool.maxSets = static_cast<uint32_t>(sets.size());
		pool.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		pool.pPoolSizes = poolSizes.data();
		check(vkCreateDescriptorPool(*mDevice, &pool, nullptr, &frame->Pool), "vkCreateDescriptorPool(RT)");
		VkDescriptorSetAllocateInfo alloc { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		alloc.descriptorPool = frame->Pool;
		alloc.descriptorSetCount = static_cast<uint32_t>(sets.size());
		alloc.pSetLayouts = data.Sets.data();
		check(vkAllocateDescriptorSets(*mDevice, &alloc, sets.data()), "vkAllocateDescriptorSets(RT)");
	}
	if (!computeList) {
		deactivateRenderPass();
	}
	const VkCommandBuffer cmd = computeList ? computeList : VkCommandBuffer(*getCurrentFrame().mCommandBuffer);
	const auto shaderStage = data.Compute ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
	std::set<const TextureVk*> sampledImages, storageImages;
	dependency(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, shaderStage,
		VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT
			| ((!data.Compute || data.RequiresRayQuery) ? VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR : 0));
	for (const auto& binding : data.Bindings) {
		const std::vector<RayTracingResource>* resources = nullptr;
		const auto numeric = std::to_string(binding.Set) + ":" + std::to_string(binding.Index);
		if (auto found = dispatch.Bindings.find(numeric); found != dispatch.Bindings.end())
			resources = &found->second;
		else
			for (const auto& name : binding.Names)
				if (auto found = dispatch.Bindings.find(name); found != dispatch.Bindings.end()) {
					resources = &found->second;
					break;
				}
		if (!resources || resources->size() != binding.Count)
			throw std::runtime_error("Missing RT binding or wrong array size at " + numeric);
		std::vector<VkDescriptorBufferInfo> buffers;
		std::vector<VkDescriptorImageInfo> images;
		std::vector<VkAccelerationStructureKHR> structures;
		for (const auto& resource : *resources) {
			if (computeList
				&& (binding.Type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
					|| !std::holds_alternative<std::shared_ptr<StorageBufferInterface>>(resource))) {
				throw std::invalid_argument("Vulkan graph compute requires storage buffer bindings");
			}
			if (binding.Type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
				const auto* ptr = std::get_if<std::shared_ptr<AccelerationStructure>>(&resource);
				auto as = ptr ? std::dynamic_pointer_cast<ASVk>(*ptr) : nullptr;
				if (!as || as->Driver != this || !as->Data->TopLevel)
					throw std::runtime_error("RT AS binding requires a TLAS from this driver");
				structures.push_back(as->Data->Handle);
			}
			else if (binding.Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
				const auto* ptr = std::get_if<std::shared_ptr<UniformBufferInterface>>(&resource);
				auto buffer = ptr ? std::dynamic_pointer_cast<UniformBufferVk>(*ptr) : nullptr;
				if (!buffer || !buffer->getBuffer())
					throw std::runtime_error("Invalid RT uniform buffer");
				buffers.push_back({ buffer->getBuffer().handle(), 0, buffer->getSizeByte() });
			}
			else if (binding.Type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
				if (const auto* ptr = std::get_if<std::shared_ptr<VertexBufferInterface>>(&resource)) {
					auto buffer = std::dynamic_pointer_cast<VertexBufferVk>(*ptr);
					if (!buffer || !buffer->getBuffer() || buffer->getBuffer().allocator() != mAllocator)
						throw std::runtime_error("Invalid compute vertex buffer");
					buffers.push_back({ buffer->getBuffer().handle(), 0, buffer->getSizeByte() });
				}
				else {
					const auto* storagePtr = std::get_if<std::shared_ptr<StorageBufferInterface>>(&resource);
					auto buffer = storagePtr ? std::dynamic_pointer_cast<StorageBufferVk>(*storagePtr) : nullptr;
					if (!buffer || !buffer->getBuffer())
						throw std::runtime_error("Invalid RT storage buffer");
					buffers.push_back({ buffer->getBuffer().handle(), 0, buffer->getSizeByte() });
				}
			}
			else {
				const auto* ptr = std::get_if<std::shared_ptr<TextureInterface>>(&resource);
				auto texture = ptr ? std::dynamic_pointer_cast<TextureVk>(*ptr) : nullptr;
				if (!texture)
					throw std::runtime_error("Invalid RT image binding");
				if (binding.Type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
					storageImages.insert(texture.get());
					if ((binding.ImageFormat == SpvImageFormatRgba32f && texture->getFormat() != PixelFormat::RGBA_FLOAT)
						|| (binding.ImageFormat == SpvImageFormatRg32f && texture->getFormat() != PixelFormat::RG_FLOAT)
						|| (binding.ImageFormat == SpvImageFormatR32f && texture->getFormat() != PixelFormat::R_FLOAT))
						throw std::runtime_error("RT storage image format disagrees with shader qualifier");
				}
				else
					sampledImages.insert(texture.get());
				texture->setState(getCurrentFrame().mCommandBuffer, vk::ImageLayout::eGeneral);
				images.push_back({ binding.Type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? VK_NULL_HANDLE : VkSampler(*texture->mSampler),
					*texture->mImageView, VK_IMAGE_LAYOUT_GENERAL });
			}
		}
		VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		write.dstSet = sets[binding.Set];
		write.dstBinding = binding.Index;
		write.descriptorType = binding.Type;
		write.descriptorCount = binding.Count;
		write.pBufferInfo = buffers.data();
		write.pImageInfo = images.data();
		VkWriteDescriptorSetAccelerationStructureKHR asWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
		asWrite.accelerationStructureCount = static_cast<uint32_t>(structures.size());
		asWrite.pAccelerationStructures = structures.data();
		if (!structures.empty())
			write.pNext = &asWrite;
		vkUpdateDescriptorSets(*mDevice, 1, &write, 0, nullptr);
	}
	const auto bindPoint = data.Compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
	for (const auto* image : sampledImages)
		if (storageImages.contains(image))
			throw std::runtime_error("RT image cannot be both a sampled input and storage output in one dispatch");
	vkCmdBindPipeline(cmd, bindPoint, data.Pipeline);
	if (!sets.empty())
		vkCmdBindDescriptorSets(cmd, bindPoint, data.Layout, 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
	if (data.PushSize)
		vkCmdPushConstants(cmd, data.Layout, VK_SHADER_STAGE_ALL, 0, data.PushSize, dispatch.PushConstants.data());
	if (frame->Timestamps) {
		vkCmdResetQueryPool(cmd, frame->Timestamps, 0, 2);
		vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, frame->Timestamps, 0);
	}
	if (data.Compute)
		vkCmdDispatch(cmd, groups[0], groups[1], groups[2]);
	else {
		auto raygen = data.Regions[0];
		raygen.deviceAddress += dispatch.RaygenRecord * raygen.stride;
		raygen.size = raygen.stride;
		vkCmdTraceRaysKHR(
			cmd, &raygen, &data.Regions[1], &data.Regions[2], &data.Regions[3], dispatch.Extent[0], dispatch.Extent[1], dispatch.Extent[2]);
	}
	dependency(cmd, shaderStage, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
	if (frame->Timestamps)
		vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, frame->Timestamps, 1);
	mRayTracingFrameResources.push_back(frame);
	if (!computeList) {
		mBoundPipelineName.clear();
		graphics_pipeline_ignore_bindings.clear();
		setDirty(Dirty::PIPELINE);
	}
}
#endif
