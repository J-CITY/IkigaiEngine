#include "virtualGeometryGpuSelection.h"

#include "profilerModule/gpuStageProfiler.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/storageBufferInterface.h"
#include "renderModule/backends/interface/uniformBufferInterface.h"
#include "renderModule/backends/interface/vertexBufferInterface.h"
#include "renderModule/render.h"
#include "resourceModule/shaderManager.h"
#include "virtualMeshAsset.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace IKIGAI::VG {
namespace {
	constexpr const char* InstanceCullShaderPath = "/shaders/virtualGeometry/vgInstanceCull.shader";
	constexpr const char* ClusterCullShaderPath = "/shaders/virtualGeometry/vgClusterCull.shader";
	constexpr const char* ViewUniformName = "VgCullView";
	constexpr const char* CullInstancesName = "engine_VgCullInstances";
	constexpr const char* AssetDispatchName = "engine_VgAssetDispatch";
	constexpr const char* VisibleInstancesName = "engine_VgVisibleInstances";
	constexpr const char* StatisticsName = "engine_VgStatistics";
	constexpr const char* ClustersName = "engine_VgClusters";
	constexpr const char* SlotBatchesName = "engine_VgSlotBatches";
	constexpr const char* SegmentsName = "engine_VgBatches";
	constexpr const char* SegmentCountsName = "engine_VgBatchCounts";
	constexpr const char* DrawCommandsName = "engine_VgDrawCommands";
	constexpr const char* DrawRecordsName = "engine_VgDrawRecords";
	constexpr uint32_t GroupSize = 64;
	// Words per asset in the dispatch buffer: DispatchIndirectCommand padded to 16 bytes.
	constexpr uint32_t AssetDispatchWords = 4;
	constexpr uint32_t SegmentWords = 4;
	constexpr uint32_t DrawRecordBytes = 16;
	constexpr uint32_t StatisticsWords = 17;
	constexpr uint32_t StatisticsBufferWords = 32;
	constexpr uint32_t MinimumCapacity = 256;
	// Upload granularity: keeps DX12 upload-ring allocations inside its 4 MiB pages.
	constexpr size_t UploadChunkBytes = 2u * 1024u * 1024u;

	uint32_t GrowCapacity(uint32_t current, size_t required) {
		uint64_t capacity = std::max<uint64_t>(current, MinimumCapacity);
		while (capacity < required) {
			capacity *= 2;
		}
		if (capacity > UINT32_MAX) {
			throw std::runtime_error("Virtual geometry GPU selection exceeds 32-bit capacity");
		}
		return static_cast<uint32_t>(capacity);
	}

	void UploadChunked(RENDER::StorageBufferInterface& buffer, const void* data, size_t bytes) {
		const auto* source = static_cast<const uint8_t*>(data);
		for (size_t offset = 0; offset < bytes; offset += UploadChunkBytes) {
			buffer.setSubData(source + offset, std::min(UploadChunkBytes, bytes - offset), offset);
		}
	}

	uint32_t CeilDiv(uint32_t value, uint32_t divisor) {
		return (value + divisor - 1) / divisor;
	}

	PROFILER::GpuStageProfiler::Scope Measure(PROFILER::GpuStageProfiler* profiler, const char* name) {
		return profiler ? profiler->measure(name) : PROFILER::GpuStageProfiler::Scope {};
	}
}

VirtualGeometryGpuSelection::VirtualGeometryGpuSelection() = default;
VirtualGeometryGpuSelection::~VirtualGeometryGpuSelection() = default;

bool VirtualGeometryGpuSelection::loadShaders() {
	if (mShaderLoadAttempted) {
		return mShadersLoaded;
	}
	mShaderLoadAttempted = true;
	try {
		mInstanceCullShader = RESOURCES::ShaderLoader::Create(InstanceCullShaderPath);
		mClusterCullShader = RESOURCES::ShaderLoader::Create(ClusterCullShaderPath);
	} catch (const std::exception& error) {
		std::cerr << "VG_GPU_SELECTION_SHADER_FAIL " << error.what() << std::endl;
	}
	mShadersLoaded = mInstanceCullShader && mClusterCullShader;
	if (!mShadersLoaded) {
		mInstanceCullShader.reset();
		mClusterCullShader.reset();
	}
	return mShadersLoaded;
}

std::string VirtualGeometryGpuSelection::unavailableReason(RENDER::Renderer& renderer) {
	if (!IsVirtualGeometryGpuPathSupported()) {
		return "backend";
	}
	const auto features = renderer.gpuFeatures();
	if (!features.Compute) {
		return "compute";
	}
	if (!features.DrawIndirectCount) {
		return "draw-indirect-count";
	}
	if (!features.DispatchIndirect) {
		return "dispatch-indirect";
	}
	if (!features.BufferTransfer) {
		return "buffer-transfer";
	}
	if (!loadShaders()) {
		return "shaders";
	}
	return {};
}

void VirtualGeometryGpuSelection::ensureCapacity(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame) {
	using RENDER::BufferUsage;
	const size_t instances = frame.Instances.size();
	if (!mCullInstanceBuffer || instances > mInstanceCapacity) {
		mInstanceCapacity = GrowCapacity(mInstanceCapacity, instances);
		mCullInstanceBuffer = renderer.createStorageBuffer(nullptr, mInstanceCapacity, sizeof(VirtualGeometryCullInstanceGpu), BufferUsage::STORAGE);
		mVisibleInstanceBuffer = renderer.createStorageBuffer(nullptr, mInstanceCapacity, sizeof(uint32_t), BufferUsage::STORAGE);
	}
	if (!mAssetDispatchBuffer || frame.Assets.size() > mAssetCapacity) {
		mAssetCapacity = GrowCapacity(mAssetCapacity, frame.Assets.size());
		mAssetDispatchBuffer = renderer.createStorageBuffer(
			nullptr, size_t(mAssetCapacity) * AssetDispatchWords, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT);
	}
	if (!mSlotBatchBuffer || frame.SlotBatches.size() > mSlotBatchCapacity) {
		mSlotBatchCapacity = GrowCapacity(mSlotBatchCapacity, frame.SlotBatches.size());
		mSlotBatchBuffer = renderer.createStorageBuffer(nullptr, mSlotBatchCapacity, sizeof(uint32_t), BufferUsage::STORAGE);
	}
	if (!mSegmentBuffer || frame.Segments.size() > mSegmentCapacity) {
		mSegmentCapacity = GrowCapacity(mSegmentCapacity, frame.Segments.size());
		mSegmentBuffer = renderer.createStorageBuffer(nullptr, size_t(mSegmentCapacity) * SegmentWords, sizeof(uint32_t), BufferUsage::STORAGE);
		mSegmentCountBuffer = renderer.createStorageBuffer(
			nullptr, mSegmentCapacity, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT | BufferUsage::READBACK);
	}
	if (!mIndirectBuffer || frame.DrawCapacity > mDrawCapacity) {
		mDrawCapacity = GrowCapacity(mDrawCapacity, frame.DrawCapacity);
		mIndirectBuffer = renderer.createStorageBuffer(
			nullptr, mDrawCapacity, sizeof(RENDER::DrawIndexedIndirectCommand), BufferUsage::STORAGE | BufferUsage::INDIRECT);
		mDrawRecordBuffer = renderer.createStorageBuffer(nullptr, mDrawCapacity, DrawRecordBytes, BufferUsage::STORAGE | BufferUsage::READBACK);
	}
	if (!mDrawIndexStream || frame.DrawCapacity > mDrawIndexCapacity) {
		mDrawIndexCapacity = GrowCapacity(mDrawIndexCapacity, frame.DrawCapacity);
		std::vector<uint32_t> identity(mDrawIndexCapacity);
		std::iota(identity.begin(), identity.end(), 0u);
		mDrawIndexStream = CreateVirtualGeometryVertexBuffer(identity.data(), identity.size(), sizeof(uint32_t));
	}
	if (!mStatisticsBuffer) {
		mStatisticsBuffer
			= renderer.createStorageBuffer(nullptr, StatisticsBufferWords, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::READBACK);
	}
	if (!mViewUniforms) {
		const VirtualGeometryCullViewGpu empty = {};
		mViewUniforms = renderer.createUniformBuffer(&empty, sizeof(empty));
	}
}

void VirtualGeometryGpuSelection::record(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame, PROFILER::GpuStageProfiler* profiler) {
	if (frame.Instances.empty() || !loadShaders()) {
		return;
	}
	ensureCapacity(renderer, frame);

	mAssetDispatchScratch.assign(frame.Assets.size() * AssetDispatchWords, 0u);
	for (size_t asset = 0; asset < frame.Assets.size(); ++asset) {
		const auto clusters = static_cast<uint32_t>(frame.Assets[asset].Asset->clusters().size());
		mAssetDispatchScratch[asset * AssetDispatchWords + 0] = CeilDiv(clusters, GroupSize);
		mAssetDispatchScratch[asset * AssetDispatchWords + 1] = 0u; // appended by vgInstanceCull
		mAssetDispatchScratch[asset * AssetDispatchWords + 2] = 1u;
	}
	mSegmentScratch.assign(frame.Segments.size() * SegmentWords, 0u);
	for (size_t segment = 0; segment < frame.Segments.size(); ++segment) {
		mSegmentScratch[segment * SegmentWords + 0] = frame.Segments[segment].FirstDraw;
		mSegmentScratch[segment * SegmentWords + 1] = frame.Segments[segment].Capacity;
	}
	UploadChunked(*mCullInstanceBuffer, frame.Instances.data(), frame.Instances.size() * sizeof(VirtualGeometryCullInstanceGpu));
	UploadChunked(*mAssetDispatchBuffer, mAssetDispatchScratch.data(), mAssetDispatchScratch.size() * sizeof(uint32_t));
	UploadChunked(*mSlotBatchBuffer, frame.SlotBatches.data(), frame.SlotBatches.size() * sizeof(uint32_t));
	UploadChunked(*mSegmentBuffer, mSegmentScratch.data(), mSegmentScratch.size() * sizeof(uint32_t));
	// Counters are reset through the upload ring rather than fillBuffer: on DirectX 12 fillBuffer creates a committed
	// upload resource per call. Measured on 64 dragons (DX12 Release): record() 1.2 ms with two fills, 0.05 ms without.
	mZeroScratch.assign(std::max<size_t>(frame.Segments.size(), StatisticsBufferWords), 0u);
	UploadChunked(*mSegmentCountBuffer, mZeroScratch.data(), frame.Segments.size() * sizeof(uint32_t));
	UploadChunked(*mStatisticsBuffer, mZeroScratch.data(), StatisticsBufferWords * sizeof(uint32_t));

	auto view = frame.View;
	view.InstanceCount = static_cast<uint32_t>(frame.Instances.size());
	{
		auto scope = Measure(profiler, "VgInstanceCull");
		mViewUniforms->setData(&view, sizeof(view));
		renderer.setShader(mInstanceCullShader);
		renderer.setUniformBuffer(ViewUniformName, mViewUniforms);
		renderer.setStorageBuffer(CullInstancesName, mCullInstanceBuffer);
		renderer.setStorageBuffer(AssetDispatchName, mAssetDispatchBuffer);
		renderer.setStorageBuffer(VisibleInstancesName, mVisibleInstanceBuffer);
		renderer.setStorageBuffer(StatisticsName, mStatisticsBuffer);
		renderer.dispatch(CeilDiv(view.InstanceCount, GroupSize), 1, 1);
	}
	{
		auto scope = Measure(profiler, "VgClusterCull");
		renderer.setShader(mClusterCullShader);
		renderer.setStorageBuffer(CullInstancesName, mCullInstanceBuffer);
		renderer.setStorageBuffer(VisibleInstancesName, mVisibleInstanceBuffer);
		renderer.setStorageBuffer(SlotBatchesName, mSlotBatchBuffer);
		renderer.setStorageBuffer(SegmentsName, mSegmentBuffer);
		renderer.setStorageBuffer(SegmentCountsName, mSegmentCountBuffer);
		renderer.setStorageBuffer(DrawCommandsName, mIndirectBuffer);
		renderer.setStorageBuffer(DrawRecordsName, mDrawRecordBuffer);
		renderer.setStorageBuffer(StatisticsName, mStatisticsBuffer);
		for (size_t asset = 0; asset < frame.Assets.size(); ++asset) {
			const auto& entry = frame.Assets[asset];
			view.AssetDispatch = static_cast<uint32_t>(asset);
			view.ClusterCount = static_cast<uint32_t>(entry.Asset->clusters().size());
			view.VisibleSegment = entry.FirstInstance;
			mViewUniforms->setData(&view, sizeof(view));
			renderer.setUniformBuffer(ViewUniformName, mViewUniforms);
			renderer.setStorageBuffer(ClustersName, entry.Asset->gpuBuffers().Clusters);
			renderer.dispatchIndirect(mAssetDispatchBuffer, asset * AssetDispatchWords * sizeof(uint32_t));
		}
	}

	if (!mAsyncReadback) {
		return;
	}
	renderer.flush();
	try {
		mPendingTicket = renderer.getDriver()->requestBufferReadback(mStatisticsBuffer, 0, StatisticsWords * sizeof(uint32_t));
		mHasPendingTicket = true;
	} catch (const RENDER::UnsupportedGpuFeature& error) {
		// Statistics are optional: without asynchronous readback the GPU counters are simply not reported.
		std::cout << "VG_GPU_STATISTICS_UNAVAILABLE " << error.what() << std::endl;
		mAsyncReadback = false;
		mHasPendingTicket = false;
	}
}

bool VirtualGeometryGpuSelection::takeStatistics(RENDER::Renderer& renderer, VirtualGeometryGpuStatistics& statistics) {
	if (!mHasPendingTicket) {
		return false;
	}
	std::vector<uint8_t> bytes;
	if (!renderer.getDriver()->collectBufferReadback(mPendingTicket, bytes)) {
		return false;
	}
	mHasPendingTicket = false;
	statistics = decodeStatistics(bytes);
	return true;
}

VirtualGeometryGpuStatistics VirtualGeometryGpuSelection::decodeStatistics(const std::vector<uint8_t>& bytes) const {
	std::array<uint32_t, StatisticsWords> words = {};
	std::memcpy(words.data(), bytes.data(), std::min(bytes.size(), sizeof(words)));
	VirtualGeometryGpuStatistics result;
	result.VisibleInstances = words[0];
	result.InstancesCulled = words[1];
	result.LodClusters = words[2];
	result.LodTriangles = words[3];
	result.VisibleClusters = words[4];
	result.VisibleTriangles = words[5];
	result.FrustumCulled = words[6];
	result.ConeCulled = words[7];
	result.Overflow = words[8];
	for (uint32_t level = 0; level < 8; ++level) {
		result.LodHistogram[level] = words[9 + level];
	}
	return result;
}

VirtualGeometryGpuCaptureTickets VirtualGeometryGpuSelection::requestCapture(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame) {
	VirtualGeometryGpuCaptureTickets tickets;
	if (frame.Segments.empty() || !mSegmentCountBuffer || !frame.DrawCapacity) {
		return tickets;
	}
	renderer.flush();
	auto* driver = renderer.getDriver();
	tickets.RecordBytes = frame.DrawCapacity * DrawRecordBytes;
	tickets.Counts = driver->requestBufferReadback(mSegmentCountBuffer, 0, frame.Segments.size() * sizeof(uint32_t));
	tickets.Records = driver->requestBufferReadback(mDrawRecordBuffer, 0, tickets.RecordBytes);
	tickets.Statistics = driver->requestBufferReadback(mStatisticsBuffer, 0, StatisticsWords * sizeof(uint32_t));
	return tickets;
}

bool VirtualGeometryGpuSelection::collectCapture(RENDER::Renderer& renderer, const VirtualGeometryGpuCaptureTickets& tickets,
	const std::vector<VirtualGeometryGpuSegment>& segments, std::vector<VirtualGeometryGpuVisibleCluster>& visible,
	VirtualGeometryGpuStatistics& statistics) {
	if (!tickets.Counts) {
		return false;
	}
	auto* driver = renderer.getDriver();
	std::vector<uint8_t> countBytes;
	std::vector<uint8_t> recordBytes;
	std::vector<uint8_t> statisticsBytes;
	// The three copies were recorded together: when the last one completed, all did.
	if (!driver->collectBufferReadback(tickets.Statistics, statisticsBytes)) {
		return false;
	}
	if (!driver->collectBufferReadback(tickets.Counts, countBytes) || !driver->collectBufferReadback(tickets.Records, recordBytes)) {
		throw std::runtime_error("Virtual geometry capture: readbacks of one frame completed out of order");
	}
	statistics = decodeStatistics(statisticsBytes);
	std::vector<uint32_t> counts(countBytes.size() / sizeof(uint32_t));
	std::memcpy(counts.data(), countBytes.data(), counts.size() * sizeof(uint32_t));
	std::vector<uint32_t> records(recordBytes.size() / sizeof(uint32_t));
	std::memcpy(records.data(), recordBytes.data(), records.size() * sizeof(uint32_t));
	visible.clear();
	for (size_t segment = 0; segment < segments.size() && segment < counts.size(); ++segment) {
		const uint32_t drawn = std::min(counts[segment], segments[segment].Capacity);
		for (uint32_t draw = 0; draw < drawn; ++draw) {
			const size_t word = (size_t(segments[segment].FirstDraw) + draw) * 4;
			visible.push_back({ records[word + 0], records[word + 1] });
		}
	}
	return true;
}

const std::shared_ptr<RENDER::StorageBufferInterface>& VirtualGeometryGpuSelection::indirectBuffer() const {
	return mIndirectBuffer;
}

const std::shared_ptr<RENDER::StorageBufferInterface>& VirtualGeometryGpuSelection::drawRecordBuffer() const {
	return mDrawRecordBuffer;
}

const std::shared_ptr<RENDER::StorageBufferInterface>& VirtualGeometryGpuSelection::segmentCountBuffer() const {
	return mSegmentCountBuffer;
}

const std::shared_ptr<RENDER::VertexBufferInterface>& VirtualGeometryGpuSelection::drawIndexStream() const {
	return mDrawIndexStream;
}

size_t VirtualGeometryGpuSelection::gpuBytes() const {
	size_t bytes = 0;
	bytes += size_t(mInstanceCapacity) * (sizeof(VirtualGeometryCullInstanceGpu) + sizeof(uint32_t));
	bytes += size_t(mAssetCapacity) * AssetDispatchWords * sizeof(uint32_t);
	bytes += size_t(mSlotBatchCapacity) * sizeof(uint32_t);
	bytes += size_t(mSegmentCapacity) * (SegmentWords + 1) * sizeof(uint32_t);
	bytes += size_t(mDrawCapacity) * (sizeof(RENDER::DrawIndexedIndirectCommand) + DrawRecordBytes);
	bytes += size_t(mDrawIndexCapacity) * sizeof(uint32_t);
	bytes += mStatisticsBuffer ? StatisticsBufferWords * sizeof(uint32_t) : 0;
	return bytes;
}
}
