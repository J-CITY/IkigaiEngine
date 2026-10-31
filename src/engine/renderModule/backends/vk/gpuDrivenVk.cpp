#include "driverVk.h"
#ifdef VULKAN_BACKEND
#include "helpers.h"
#include "storageBufferVk.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace IKIGAI::RENDER {
namespace {
	StorageBufferVk& NativeBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, const char* operation) {
		auto* native = dynamic_cast<StorageBufferVk*>(buffer.get());
		if (!native || !native->getBuffer()) {
			throw std::invalid_argument(std::string(operation) + ": missing or empty Vulkan storage buffer");
		}
		return *native;
	}

	void CheckRange(const StorageBufferInterface& buffer, size_t offset, size_t size, const char* operation) {
		if (offset > buffer.getSizeByte() || size > buffer.getSizeByte() - offset) {
			throw std::invalid_argument(std::string(operation) + ": range exceeds buffer size");
		}
	}

	void CheckUsage(const StorageBufferVk& buffer, BufferUsage usage, const char* operation) {
		if (!HasBufferUsage(buffer.getUsage(), usage)) {
			throw std::invalid_argument(std::string(operation) + ": buffer was created without the required BufferUsage flag");
		}
	}

	void CheckTransferSource(const StorageBufferVk& buffer, const char* operation) {
		if (!HasBufferUsage(buffer.getUsage(), BufferUsage::TRANSFER_SRC) && !HasBufferUsage(buffer.getUsage(), BufferUsage::READBACK)) {
			throw std::invalid_argument(std::string(operation) + ": buffer needs BufferUsage::TRANSFER_SRC or READBACK");
		}
	}

	void CheckIndirectArguments(const StorageBufferVk& buffer, size_t offset, uint32_t drawCount, uint32_t stride, const char* operation) {
		CheckUsage(buffer, BufferUsage::INDIRECT, operation);
		if (offset % 4 != 0 || stride % 4 != 0 || stride < sizeof(DrawIndexedIndirectCommand)) {
			throw std::invalid_argument(std::string(operation) + ": offset and stride must be multiples of 4 and stride >= 20");
		}
		const uint64_t end = uint64_t(offset) + uint64_t(drawCount - 1) * stride + sizeof(DrawIndexedIndirectCommand);
		if (end > buffer.getSizeByte()) {
			throw std::invalid_argument(std::string(operation) + ": indirect arguments exceed buffer size");
		}
	}

	void LogGpuFeaturesVk(const GpuFeatures& features) {
		std::cout << "GPU_FEATURES backend=vulkan compute=" << features.Compute << " usageFlags=" << features.BufferUsageFlags
				  << " transfer=" << features.BufferTransfer << " readback=" << features.BufferReadback << " drawIndirect=" << features.DrawIndirect
				  << " drawIndirectCount=" << features.DrawIndirectCount << " dispatchIndirect=" << features.DispatchIndirect
				  << " subgroupBasic=" << features.SubgroupBasic << " subgroupBallot=" << features.SubgroupBallot
				  << " subgroupSize=" << features.SubgroupSize << " int64BufferAtomics=" << features.BufferInt64Atomics
				  << " maxDrawIndirectCount=" << features.MaxDrawIndirectCount << " maxWorkGroups=" << features.MaxComputeWorkGroupCountX << "x"
				  << features.MaxComputeWorkGroupCountY << "x" << features.MaxComputeWorkGroupCountZ << std::endl;
	}
}

void DriverVk::configureGpuFeatures(std::vector<const char*>& extensions) {
	(void)extensions; // Everything used here is Vulkan 1.0-1.2 core.
	const auto properties = mPhysicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceSubgroupProperties>();
	const auto& core = properties.get<vk::PhysicalDeviceProperties2>().properties;
	const auto& subgroup = properties.get<vk::PhysicalDeviceSubgroupProperties>();
	const auto features = mPhysicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features>();
	const auto& coreFeatures = features.get<vk::PhysicalDeviceFeatures2>().features;
	const auto& vulkan12 = features.get<vk::PhysicalDeviceVulkan12Features>();
	const bool vulkan12Device = core.apiVersion >= VK_API_VERSION_1_2;

	GpuFeatures result;
	result.Compute = true;
	result.BufferUsageFlags = true;
	result.BufferTransfer = true;
	result.BufferReadback = true;
	result.DrawIndirect = true;
	result.DispatchIndirect = true;
	// multiDrawIndirect is passed through from the queried core features at device creation.
	result.MaxDrawIndirectCount = coreFeatures.multiDrawIndirect ? core.limits.maxDrawIndirectCount : 1u;
	result.DrawIndirectCount = vulkan12Device && vulkan12.drawIndirectCount && coreFeatures.multiDrawIndirect;
	if (result.DrawIndirectCount) {
		mRTCoreFeatures.drawIndirectCount = VK_TRUE;
	}
	const bool computeSubgroups = static_cast<bool>(subgroup.supportedStages & vk::ShaderStageFlagBits::eCompute);
	result.SubgroupBasic = computeSubgroups && static_cast<bool>(subgroup.supportedOperations & vk::SubgroupFeatureFlagBits::eBasic);
	result.SubgroupBallot = result.SubgroupBasic && static_cast<bool>(subgroup.supportedOperations & vk::SubgroupFeatureFlagBits::eBallot);
	result.SubgroupSize = result.SubgroupBasic ? subgroup.subgroupSize : 0u;
	result.BufferInt64Atomics = vulkan12Device && vulkan12.shaderBufferInt64Atomics && coreFeatures.shaderInt64;
	if (result.BufferInt64Atomics) {
		mRTCoreFeatures.shaderBufferInt64Atomics = VK_TRUE;
	}
	result.MaxComputeWorkGroupCountX = core.limits.maxComputeWorkGroupCount[0];
	result.MaxComputeWorkGroupCountY = core.limits.maxComputeWorkGroupCount[1];
	result.MaxComputeWorkGroupCountZ = core.limits.maxComputeWorkGroupCount[2];
	// Owned by agent C (textures / samplers).
	result.StorageImageMipViews = false;
	result.HalfFloatTextures = false;
	result.SamplerMinMaxReduction = false;
	// Timestamp fields are filled in gpuFeatures() once the query pool exists.
	result.TimestampQueries = false;
	result.TimestampPeriodNanoseconds = 0.0;
	mGpuFeatures = result;
	LogGpuFeaturesVk(mGpuFeatures);
}

GpuFeatures DriverVk::gpuFeatures() const {
	auto features = mGpuFeatures;
	fillTextureGpuFeatures(features);
	// Timestamp queries are created after configureGpuFeatures() (timestampQueries*.cpp).
	features.TimestampQueries = mTimestampCapacity > 0;
	features.TimestampPeriodNanoseconds = mTimestampPeriodNanoseconds;
	return features;
}

std::shared_ptr<StorageBufferInterface> DriverVk::createStorageBuffer(const void* data, size_t count, size_t stride, BufferUsage usage) {
	return std::make_shared<StorageBufferVk>(const_cast<void*>(data), count, stride, usage);
}

void DriverVk::beginTransferCommands(vk::raii::CommandBuffer& cmd) {
	deactivateRenderPass();
	// Earlier graphics reads/writes are ordered by the tracked stage; compute dispatches
	// already end with a compute -> all-commands barrier.
	EnsureMemoryState(cmd, vk::PipelineStageFlagBits2::eTransfer);
}

void DriverVk::endTransferCommands(vk::raii::CommandBuffer& cmd) {
	// Make transfer writes visible to every later consumer: compute, indirect argument,
	// vertex/index input and further transfers.
	UtilityVk::SetMemoryBarrier(cmd, vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eAllCommands);
}

void DriverVk::fillBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size, uint32_t value) {
	auto& native = NativeBuffer(buffer, "fillBuffer");
	if (!size) {
		return;
	}
	if (offset % 4 != 0 || size % 4 != 0) {
		throw std::invalid_argument("fillBuffer: offset and size must be multiples of 4");
	}
	CheckRange(native, offset, size, "fillBuffer");
	auto& cmd = getCurrentFrame().mCommandBuffer;
	beginTransferCommands(cmd);
	cmd.fillBuffer(*native.getBuffer(), offset, size, value);
	endTransferCommands(cmd);
}

void DriverVk::copyBuffer(const std::shared_ptr<StorageBufferInterface>& source, size_t sourceOffset,
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
	auto& cmd = getCurrentFrame().mCommandBuffer;
	beginTransferCommands(cmd);
	vk::BufferCopy region;
	region.setSrcOffset(sourceOffset).setDstOffset(destinationOffset).setSize(size);
	cmd.copyBuffer(*from.getBuffer(), *to.getBuffer(), { region });
	endTransferCommands(cmd);
}

std::vector<uint8_t> DriverVk::readBuffer(const std::shared_ptr<StorageBufferInterface>& buffer, size_t offset, size_t size) {
	auto& native = NativeBuffer(buffer, "readBuffer");
	std::vector<uint8_t> result(size);
	if (!size) {
		return result;
	}
	CheckTransferSource(native, "readBuffer");
	CheckRange(native, offset, size, "readBuffer");
	auto staging = UtilityVk::CreateBuffer(size, vk::BufferUsageFlagBits::eTransferDst, UtilityVk::MemoryUsage::Staging);
	auto& cmd = getCurrentFrame().mCommandBuffer;
	beginTransferCommands(cmd);
	vk::BufferCopy region;
	region.setSrcOffset(offset).setDstOffset(0).setSize(size);
	cmd.copyBuffer(*native.getBuffer(), *staging, { region });
	UtilityVk::SetMemoryBarrier(cmd, vk::PipelineStageFlagBits2::eTransfer, vk::PipelineStageFlagBits2::eHost);
	endTransferCommands(cmd);
	flushRecordedCommands();
	void* mapped = nullptr;
	if (vmaMapMemory(mAllocator, staging.allocation(), &mapped) != VK_SUCCESS) {
		throw std::runtime_error("readBuffer: staging map failed");
	}
	vmaInvalidateAllocation(mAllocator, staging.allocation(), 0, VK_WHOLE_SIZE);
	std::memcpy(result.data(), mapped, size);
	vmaUnmapMemory(mAllocator, staging.allocation());
	return result;
}

void DriverVk::flushRecordedCommands() {
	// Submits everything recorded so far in this frame and waits for it, then reopens the
	// frame command buffer. The recorded prologue already transitioned the swapchain image,
	// so the submission waits on the acquire semaphore and re-signals it for end().
	deactivateRenderPass();
	auto& frame = getCurrentFrame();
	frame.mCommandBuffer.end();
	const auto& acquired = getCurrentSwapchainContext().frames.at(mSemaphoreIndex).mImageAcquiredSemaphore;
	const vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eAllCommands;
	vk::SubmitInfo work;
	work.setWaitSemaphores(*acquired).setWaitDstStageMask(waitStage).setCommandBuffers(*frame.mCommandBuffer);
	mQueue.submit(work);
	mQueue.waitIdle();
	vk::SubmitInfo resignal;
	resignal.setSignalSemaphores(*acquired);
	mQueue.submit(resignal);

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	frame.mCommandBuffer.begin(beginInfo);
	mBoundPipelineName.clear();
	graphics_pipeline_ignore_bindings.clear();
	for (auto flag : { Dirty::VERTEX_BUFFER, Dirty::INDEX_BUFFER, Dirty::VIEWPORT, Dirty::SCISSOR, Dirty::RASTERIZATION_MODE, Dirty::PRIMITIVE_MODE,
		 Dirty::DEPTH, Dirty::STENCIL, Dirty::BLENDING, Dirty::CULLING, Dirty::PIPELINE, Dirty::TRIANGULATION_ORDER }) {
		setDirty(flag);
	}
}

void DriverVk::drawIndexedIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset, uint32_t drawCount, uint32_t stride) {
	auto& native = NativeBuffer(arguments, "drawIndexedIndirect");
	if (!drawCount) {
		return;
	}
	if (drawCount > mGpuFeatures.MaxDrawIndirectCount) {
		throw UnsupportedGpuFeature("drawIndexedIndirect: drawCount exceeds MaxDrawIndirectCount");
	}
	CheckIndirectArguments(native, offset, drawCount, stride, "drawIndexedIndirect");
	validateVertexDraw({ 1, 0, 1, 0, 0, true });
	EnsureGraphicsState(true);
	getCurrentFrame().mCommandBuffer.drawIndexedIndirect(*native.getBuffer(), offset, drawCount, stride);
}

void DriverVk::drawIndexedIndirectCount(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset,
	const std::shared_ptr<StorageBufferInterface>& count, size_t countOffset, uint32_t maxDrawCount, uint32_t stride) {
	if (!mGpuFeatures.DrawIndirectCount) {
		throw UnsupportedGpuFeature("drawIndexedIndirectCount requires Vulkan 1.2 drawIndirectCount");
	}
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
	EnsureGraphicsState(true);
	getCurrentFrame().mCommandBuffer.drawIndexedIndirectCount(
		*native.getBuffer(), offset, *counter.getBuffer(), countOffset, std::min(maxDrawCount, mGpuFeatures.MaxDrawIndirectCount), stride);
}

void DriverVk::dispatchIndirect(const std::shared_ptr<StorageBufferInterface>& arguments, size_t offset) {
	auto& native = NativeBuffer(arguments, "dispatchIndirect");
	CheckUsage(native, BufferUsage::INDIRECT, "dispatchIndirect");
	if (offset % 4 != 0) {
		throw std::invalid_argument("dispatchIndirect: offset must be a multiple of 4");
	}
	CheckRange(native, offset, sizeof(DispatchIndirectCommand), "dispatchIndirect");
	auto& cmd = getCurrentFrame().mCommandBuffer;
	EnsureComputeState(cmd);
	cmd.dispatchIndirect(*native.getBuffer(), offset);
	// The argument read happens in the draw-indirect stage: include it so later writes to the
	// argument buffer (compute or transfer) are ordered after the read.
	UtilityVk::SetMemoryBarrier(
		cmd, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eDrawIndirect, vk::PipelineStageFlagBits2::eAllCommands);
}
}
#endif
