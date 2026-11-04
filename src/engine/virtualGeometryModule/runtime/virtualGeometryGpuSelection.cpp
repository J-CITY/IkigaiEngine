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
	constexpr const char* InstanceCullPostShaderPath = "/shaders/virtualGeometry/vgInstanceCullPost.shader";
	constexpr const char* ClusterCullPostShaderPath = "/shaders/virtualGeometry/vgClusterCullPost.shader";
	constexpr const char* ViewUniformName = "VgCullView";
	constexpr const char* HzbName = "VgHzb";
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
	constexpr const char* OccludedInstancesName = "engine_VgOccludedInstances";
	constexpr const char* PostInstanceArgsName = "engine_VgPostInstanceArgs";
	constexpr const char* PostInstanceDispatchName = "engine_VgPostInstanceDispatch";
	constexpr const char* PostVisibleInstancesName = "engine_VgPostVisibleInstances";
	constexpr const char* OccludedClustersName = "engine_VgOccludedClusters";
	constexpr const char* OccludedClusterCountsName = "engine_VgOccludedClusterCounts";
	constexpr const char* PostClusterArgsName = "engine_VgPostClusterArgs";
	constexpr uint32_t GroupSize = 64;
	// Words per asset in the dispatch buffers: DispatchIndirectCommand padded to 16 bytes.
	constexpr uint32_t AssetDispatchWords = 4;
	constexpr uint32_t SegmentWords = 4;
	constexpr uint32_t DrawRecordBytes = 16;
	constexpr uint32_t OccludedClusterBytes = 8;
	constexpr uint32_t StatisticsWords = 25;
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

	void CopyMatrix(const float (&source)[4][4], float (&target)[4][4]) {
		std::memcpy(target, source, sizeof(target));
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
		mInstanceCullPostShader = RESOURCES::ShaderLoader::Create(InstanceCullPostShaderPath);
		mClusterCullPostShader = RESOURCES::ShaderLoader::Create(ClusterCullPostShaderPath);
	} catch (const std::exception& error) {
		std::cerr << "VG_GPU_SELECTION_SHADER_FAIL " << error.what() << std::endl;
	}
	mShadersLoaded = mInstanceCullShader && mClusterCullShader && mInstanceCullPostShader && mClusterCullPostShader;
	if (!mShadersLoaded) {
		mInstanceCullShader.reset();
		mClusterCullShader.reset();
		mInstanceCullPostShader.reset();
		mClusterCullPostShader.reset();
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
		mPostVisibleInstanceBuffer = renderer.createStorageBuffer(nullptr, mInstanceCapacity, sizeof(uint32_t), BufferUsage::STORAGE);
		// Word 0 is the append counter.
		mOccludedInstanceBuffer = renderer.createStorageBuffer(nullptr, size_t(mInstanceCapacity) + 1, sizeof(uint32_t), BufferUsage::STORAGE);
	}
	if (!mAssetDispatchBuffer || frame.Assets.size() > mAssetCapacity) {
		mAssetCapacity = GrowCapacity(mAssetCapacity, frame.Assets.size());
		const size_t words = size_t(mAssetCapacity) * AssetDispatchWords;
		mAssetDispatchBuffer = renderer.createStorageBuffer(nullptr, words, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT);
		mPostInstanceDispatchBuffer = renderer.createStorageBuffer(nullptr, words, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT);
		mPostClusterArgsBuffer = renderer.createStorageBuffer(nullptr, words, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT);
		mOccludedClusterCountBuffer = renderer.createStorageBuffer(nullptr, mAssetCapacity, sizeof(uint32_t), BufferUsage::STORAGE);
	}
	if (!mSlotBatchBuffer || frame.SlotBatches.size() > mSlotBatchCapacity) {
		mSlotBatchCapacity = GrowCapacity(mSlotBatchCapacity, frame.SlotBatches.size());
		mSlotBatchBuffer = renderer.createStorageBuffer(nullptr, mSlotBatchCapacity, sizeof(uint32_t), BufferUsage::STORAGE);
	}
	// Phase-1 segments followed by their post copies.
	const size_t segments = frame.Segments.size() * 2;
	if (!mSegmentBuffer || segments > mSegmentCapacity) {
		mSegmentCapacity = GrowCapacity(mSegmentCapacity, segments);
		mSegmentBuffer = renderer.createStorageBuffer(nullptr, size_t(mSegmentCapacity) * SegmentWords, sizeof(uint32_t), BufferUsage::STORAGE);
		mSegmentCountBuffer = renderer.createStorageBuffer(
			nullptr, mSegmentCapacity, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT | BufferUsage::READBACK);
	}
	// The post copies double the draw range only on frames with occlusion.
	const size_t draws = size_t(frame.DrawCapacity) * (frame.Occlusion.Active ? 2 : 1);
	if (!mIndirectBuffer || draws > mDrawCapacity) {
		mDrawCapacity = GrowCapacity(mDrawCapacity, draws);
		mIndirectBuffer = renderer.createStorageBuffer(
			nullptr, mDrawCapacity, sizeof(RENDER::DrawIndexedIndirectCommand), BufferUsage::STORAGE | BufferUsage::INDIRECT);
		mDrawRecordBuffer = renderer.createStorageBuffer(nullptr, mDrawCapacity, DrawRecordBytes, BufferUsage::STORAGE | BufferUsage::READBACK);
	}
	if (!mDrawIndexStream || draws > mDrawIndexCapacity) {
		mDrawIndexCapacity = GrowCapacity(mDrawIndexCapacity, draws);
		std::vector<uint32_t> identity(mDrawIndexCapacity);
		std::iota(identity.begin(), identity.end(), 0u);
		mDrawIndexStream = CreateVirtualGeometryVertexBuffer(identity.data(), identity.size(), sizeof(uint32_t));
	}
	if (!mOccludedClusterBuffer || frame.Occlusion.OccludedCapacity > mOccludedCapacity) {
		mOccludedCapacity = GrowCapacity(mOccludedCapacity, frame.Occlusion.OccludedCapacity);
		mOccludedClusterBuffer = renderer.createStorageBuffer(nullptr, mOccludedCapacity, OccludedClusterBytes, BufferUsage::STORAGE);
	}
	if (!mPostInstanceArgsBuffer) {
		mPostInstanceArgsBuffer
			= renderer.createStorageBuffer(nullptr, AssetDispatchWords, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::INDIRECT);
	}
	if (!mStatisticsBuffer) {
		mStatisticsBuffer
			= renderer.createStorageBuffer(nullptr, StatisticsBufferWords, sizeof(uint32_t), BufferUsage::STORAGE | BufferUsage::READBACK);
	}
}

void VirtualGeometryGpuSelection::setViewUniforms(RENDER::Renderer& renderer, const VirtualGeometryCullViewGpu& view) {
	if (mViewUniformsUsed == mViewUniformPool.size()) {
		mViewUniformPool.push_back(renderer.createUniformBuffer(&view, sizeof(view)));
	}
	auto& buffer = mViewUniformPool[mViewUniformsUsed++];
	buffer->setData(&view, sizeof(view));
	renderer.setUniformBuffer(ViewUniformName, buffer);
}

void VirtualGeometryGpuSelection::bindClusterCullBuffers(
	RENDER::Renderer& renderer, const std::shared_ptr<RENDER::StorageBufferInterface>& visibleInstances) {
	renderer.setStorageBuffer(CullInstancesName, mCullInstanceBuffer);
	renderer.setStorageBuffer(VisibleInstancesName, visibleInstances);
	renderer.setStorageBuffer(SlotBatchesName, mSlotBatchBuffer);
	renderer.setStorageBuffer(SegmentsName, mSegmentBuffer);
	renderer.setStorageBuffer(SegmentCountsName, mSegmentCountBuffer);
	renderer.setStorageBuffer(DrawCommandsName, mIndirectBuffer);
	renderer.setStorageBuffer(DrawRecordsName, mDrawRecordBuffer);
	renderer.setStorageBuffer(StatisticsName, mStatisticsBuffer);
	renderer.setStorageBuffer(OccludedClustersName, mOccludedClusterBuffer);
	renderer.setStorageBuffer(OccludedClusterCountsName, mOccludedClusterCountBuffer);
	renderer.setStorageBuffer(PostClusterArgsName, mPostClusterArgsBuffer);
}

void VirtualGeometryGpuSelection::record(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame, PROFILER::GpuStageProfiler* profiler) {
	if (frame.Instances.empty() || !loadShaders()) {
		return;
	}
	ensureCapacity(renderer, frame);
	mViewUniformsUsed = 0;
	const auto& occlusion = frame.Occlusion;
	const auto assets = static_cast<uint32_t>(frame.Assets.size());
	const auto segments = static_cast<uint32_t>(frame.Segments.size());

	mAssetDispatchScratch.assign(size_t(assets) * AssetDispatchWords, 0u);
	mPostDispatchScratch.assign(size_t(assets) * AssetDispatchWords, 0u);
	for (uint32_t asset = 0; asset < assets; ++asset) {
		const auto clusters = static_cast<uint32_t>(frame.Assets[asset].Asset->clusters().size());
		mAssetDispatchScratch[asset * AssetDispatchWords + 0] = CeilDiv(clusters, GroupSize);
		mAssetDispatchScratch[asset * AssetDispatchWords + 1] = 0u; // appended by vgInstanceCull
		mAssetDispatchScratch[asset * AssetDispatchWords + 2] = 1u;
		// Post cluster lists: X appended by vgClusterCull (one group per 64 entries).
		mPostDispatchScratch[asset * AssetDispatchWords + 0] = 0u;
		mPostDispatchScratch[asset * AssetDispatchWords + 1] = 1u;
		mPostDispatchScratch[asset * AssetDispatchWords + 2] = 1u;
	}
	mSegmentScratch.assign(size_t(segments) * 2 * SegmentWords, 0u);
	for (uint32_t segment = 0; segment < segments; ++segment) {
		const auto& entry = frame.Segments[segment];
		mSegmentScratch[segment * SegmentWords + 0] = entry.FirstDraw;
		mSegmentScratch[segment * SegmentWords + 1] = entry.Capacity;
		// Post copy: same capacity (a cluster is drawn by one phase only), draws after the phase-1 range.
		mSegmentScratch[(segments + segment) * SegmentWords + 0] = entry.FirstDraw + frame.DrawCapacity;
		mSegmentScratch[(segments + segment) * SegmentWords + 1] = occlusion.Active ? entry.Capacity : 0u;
	}
	UploadChunked(*mCullInstanceBuffer, frame.Instances.data(), frame.Instances.size() * sizeof(VirtualGeometryCullInstanceGpu));
	UploadChunked(*mAssetDispatchBuffer, mAssetDispatchScratch.data(), mAssetDispatchScratch.size() * sizeof(uint32_t));
	// The post instance dispatch starts as the main one: X over clusters, Y appended by vgInstanceCullPost.
	UploadChunked(*mPostInstanceDispatchBuffer, mAssetDispatchScratch.data(), mAssetDispatchScratch.size() * sizeof(uint32_t));
	UploadChunked(*mPostClusterArgsBuffer, mPostDispatchScratch.data(), mPostDispatchScratch.size() * sizeof(uint32_t));
	UploadChunked(*mSlotBatchBuffer, frame.SlotBatches.data(), frame.SlotBatches.size() * sizeof(uint32_t));
	UploadChunked(*mSegmentBuffer, mSegmentScratch.data(), mSegmentScratch.size() * sizeof(uint32_t));
	// Counters are reset through the upload ring rather than fillBuffer: on DirectX 12 fillBuffer creates a committed
	// upload resource per call. Measured on 64 dragons (DX12 Release): record() 1.2 ms with two fills, 0.05 ms without.
	mZeroScratch.assign(std::max<size_t>({ size_t(segments) * 2, size_t(assets), StatisticsBufferWords }), 0u);
	UploadChunked(*mSegmentCountBuffer, mZeroScratch.data(), size_t(segments) * 2 * sizeof(uint32_t));
	UploadChunked(*mStatisticsBuffer, mZeroScratch.data(), StatisticsBufferWords * sizeof(uint32_t));
	UploadChunked(*mOccludedClusterCountBuffer, mZeroScratch.data(), size_t(assets) * sizeof(uint32_t));
	UploadChunked(*mOccludedInstanceBuffer, mZeroScratch.data(), sizeof(uint32_t));
	const uint32_t postInstanceArgs[AssetDispatchWords] = { 0u, 1u, 1u, 0u };
	UploadChunked(*mPostInstanceArgsBuffer, postInstanceArgs, sizeof(postInstanceArgs));

	// Phase 1 tests against the previous HZB only with a valid history; the descriptor is always bound.
	const bool phaseOneOcclusion = occlusion.Active && occlusion.History && occlusion.PreviousHzb;
	const auto& hzb = phaseOneOcclusion ? occlusion.PreviousHzb : occlusion.FallbackTexture;
	auto view = frame.View;
	view.InstanceCount = static_cast<uint32_t>(frame.Instances.size());
	view.PassMode = static_cast<uint32_t>(VirtualGeometryCullPass::MAIN);
	view.AssetCount = assets;
	view.OcclusionTest = phaseOneOcclusion ? 1u : 0u;
	view.SegmentOffset = 0u;
	view.OcclusionDebug = occlusion.DebugFlags;
	CopyMatrix(occlusion.PreviousViewProjection, view.OcclusionViewProjection);
	{
		auto scope = Measure(profiler, "VgInstanceCull");
		renderer.setShader(mInstanceCullShader);
		setViewUniforms(renderer, view);
		renderer.setStorageBuffer(CullInstancesName, mCullInstanceBuffer);
		renderer.setStorageBuffer(AssetDispatchName, mAssetDispatchBuffer);
		renderer.setStorageBuffer(VisibleInstancesName, mVisibleInstanceBuffer);
		renderer.setStorageBuffer(StatisticsName, mStatisticsBuffer);
		renderer.setStorageBuffer(OccludedInstancesName, mOccludedInstanceBuffer);
		renderer.setStorageBuffer(PostInstanceArgsName, mPostInstanceArgsBuffer);
		renderer.setTexture(HzbName, hzb);
		renderer.dispatch(CeilDiv(view.InstanceCount, GroupSize), 1, 1);
	}
	{
		auto scope = Measure(profiler, "VgClusterCull");
		renderer.setShader(mClusterCullShader);
		bindClusterCullBuffers(renderer, mVisibleInstanceBuffer);
		renderer.setTexture(HzbName, hzb);
		for (uint32_t asset = 0; asset < assets; ++asset) {
			const auto& entry = frame.Assets[asset];
			view.AssetDispatch = asset;
			view.ClusterCount = static_cast<uint32_t>(entry.Asset->clusters().size());
			view.VisibleSegment = entry.FirstInstance;
			view.OccludedBase = entry.OccludedBase;
			view.OccludedCapacity = phaseOneOcclusion ? entry.OccludedCapacity : 0u;
			setViewUniforms(renderer, view);
			renderer.setStorageBuffer(ClustersName, entry.Asset->gpuBuffers().Clusters);
			renderer.dispatchIndirect(mAssetDispatchBuffer, size_t(asset) * AssetDispatchWords * sizeof(uint32_t));
		}
	}
	if (!occlusion.Active) {
		requestStatisticsReadback(renderer);
	}
}

void VirtualGeometryGpuSelection::recordPost(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame,
	const std::shared_ptr<RENDER::TextureInterface>& hzb, PROFILER::GpuStageProfiler* profiler) {
	if (frame.Instances.empty() || !frame.Occlusion.Active || !mShadersLoaded || !hzb) {
		return;
	}
	if ((frame.Occlusion.DebugFlags & VG_OCCLUSION_DEBUG_SKIP_POST) != 0u) {
		// Negative control: phase-1 rejections stay rejected.
		requestStatisticsReadback(renderer);
		return;
	}
	const auto assets = static_cast<uint32_t>(frame.Assets.size());
	auto view = frame.View;
	view.InstanceCount = static_cast<uint32_t>(frame.Instances.size());
	view.AssetCount = assets;
	view.OcclusionTest = 1u;
	view.SegmentOffset = static_cast<uint32_t>(frame.Segments.size());
	view.OcclusionDebug = frame.Occlusion.DebugFlags;
	CopyMatrix(frame.Occlusion.ViewProjection, view.OcclusionViewProjection);
	{
		auto scope = Measure(profiler, "VgInstanceCullPost");
		view.PassMode = static_cast<uint32_t>(VirtualGeometryCullPass::POST_INSTANCES);
		renderer.setShader(mInstanceCullPostShader);
		setViewUniforms(renderer, view);
		renderer.setStorageBuffer(CullInstancesName, mCullInstanceBuffer);
		renderer.setStorageBuffer(OccludedInstancesName, mOccludedInstanceBuffer);
		renderer.setStorageBuffer(PostInstanceDispatchName, mPostInstanceDispatchBuffer);
		renderer.setStorageBuffer(PostVisibleInstancesName, mPostVisibleInstanceBuffer);
		renderer.setStorageBuffer(StatisticsName, mStatisticsBuffer);
		renderer.setTexture(HzbName, hzb);
		renderer.dispatchIndirect(mPostInstanceArgsBuffer, 0);
	}
	{
		auto scope = Measure(profiler, "VgClusterCullPost");
		// Revived instances: the full cluster test, occlusion against the current HZB.
		renderer.setShader(mClusterCullShader);
		bindClusterCullBuffers(renderer, mPostVisibleInstanceBuffer);
		renderer.setTexture(HzbName, hzb);
		for (uint32_t asset = 0; asset < assets; ++asset) {
			const auto& entry = frame.Assets[asset];
			view.AssetDispatch = asset;
			view.ClusterCount = static_cast<uint32_t>(entry.Asset->clusters().size());
			view.VisibleSegment = entry.FirstInstance;
			view.OccludedBase = entry.OccludedBase;
			view.OccludedCapacity = 0u;
			setViewUniforms(renderer, view);
			renderer.setStorageBuffer(ClustersName, entry.Asset->gpuBuffers().Clusters);
			renderer.dispatchIndirect(mPostInstanceDispatchBuffer, size_t(asset) * AssetDispatchWords * sizeof(uint32_t));
		}
		// Clusters listed by phase 1: only the occlusion test is repeated.
		view.PassMode = static_cast<uint32_t>(VirtualGeometryCullPass::POST_CLUSTERS);
		renderer.setShader(mClusterCullPostShader);
		renderer.setStorageBuffer(CullInstancesName, mCullInstanceBuffer);
		renderer.setStorageBuffer(OccludedClustersName, mOccludedClusterBuffer);
		renderer.setStorageBuffer(OccludedClusterCountsName, mOccludedClusterCountBuffer);
		renderer.setStorageBuffer(SlotBatchesName, mSlotBatchBuffer);
		renderer.setStorageBuffer(SegmentsName, mSegmentBuffer);
		renderer.setStorageBuffer(SegmentCountsName, mSegmentCountBuffer);
		renderer.setStorageBuffer(DrawCommandsName, mIndirectBuffer);
		renderer.setStorageBuffer(DrawRecordsName, mDrawRecordBuffer);
		renderer.setStorageBuffer(StatisticsName, mStatisticsBuffer);
		renderer.setTexture(HzbName, hzb);
		for (uint32_t asset = 0; asset < assets; ++asset) {
			const auto& entry = frame.Assets[asset];
			view.AssetDispatch = asset;
			view.ClusterCount = static_cast<uint32_t>(entry.Asset->clusters().size());
			view.VisibleSegment = entry.FirstInstance;
			view.OccludedBase = entry.OccludedBase;
			view.OccludedCapacity = entry.OccludedCapacity;
			setViewUniforms(renderer, view);
			renderer.setStorageBuffer(ClustersName, entry.Asset->gpuBuffers().Clusters);
			renderer.dispatchIndirect(mPostClusterArgsBuffer, size_t(asset) * AssetDispatchWords * sizeof(uint32_t));
		}
	}
	requestStatisticsReadback(renderer);
}

void VirtualGeometryGpuSelection::requestStatisticsReadback(RENDER::Renderer& renderer) {
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
	result.InstancesOccludedMain = words[17];
	result.InstancesRevived = words[18];
	result.ClustersOccludedMain = words[19];
	result.ClustersRevived = words[20];
	result.ClustersPostInstances = words[21];
	result.ClustersOccludedPost = words[22];
	result.PostTriangles = words[23];
	result.OccludedListOverflow = words[24];
	return result;
}

VirtualGeometryGpuCaptureTickets VirtualGeometryGpuSelection::requestCapture(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame) {
	VirtualGeometryGpuCaptureTickets tickets;
	if (frame.Segments.empty() || !mSegmentCountBuffer || !frame.DrawCapacity) {
		return tickets;
	}
	renderer.flush();
	auto* driver = renderer.getDriver();
	const uint32_t phases = frame.Occlusion.Active ? 2u : 1u;
	tickets.RecordBytes = frame.DrawCapacity * phases * DrawRecordBytes;
	tickets.PostSegmentOffset = frame.Occlusion.Active ? static_cast<uint32_t>(frame.Segments.size()) : 0u;
	tickets.PostDrawOffset = frame.Occlusion.Active ? frame.DrawCapacity : 0u;
	tickets.Counts = driver->requestBufferReadback(mSegmentCountBuffer, 0, frame.Segments.size() * phases * sizeof(uint32_t));
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
	const auto collect = [&](size_t countIndex, uint32_t firstDraw, uint32_t capacity) {
		if (countIndex >= counts.size()) {
			return;
		}
		const uint32_t drawn = std::min(counts[countIndex], capacity);
		for (uint32_t draw = 0; draw < drawn; ++draw) {
			const size_t word = (size_t(firstDraw) + draw) * 4;
			visible.push_back({ records[word + 0], records[word + 1] });
		}
	};
	for (size_t segment = 0; segment < segments.size(); ++segment) {
		collect(segment, segments[segment].FirstDraw, segments[segment].Capacity);
		if (tickets.PostSegmentOffset) {
			collect(segment + tickets.PostSegmentOffset, segments[segment].FirstDraw + tickets.PostDrawOffset, segments[segment].Capacity);
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
	// Cull instances, visible list, post visible list, occluded-instance list.
	bytes += size_t(mInstanceCapacity) * (sizeof(VirtualGeometryCullInstanceGpu) + 3 * sizeof(uint32_t));
	bytes += mOccludedInstanceBuffer ? sizeof(uint32_t) : 0;
	// Main, post-instance and post-cluster dispatch arguments, occluded-cluster counters.
	bytes += size_t(mAssetCapacity) * (3 * AssetDispatchWords + 1) * sizeof(uint32_t);
	bytes += size_t(mSlotBatchCapacity) * sizeof(uint32_t);
	bytes += size_t(mSegmentCapacity) * (SegmentWords + 1) * sizeof(uint32_t);
	bytes += size_t(mDrawCapacity) * (sizeof(RENDER::DrawIndexedIndirectCommand) + DrawRecordBytes);
	bytes += size_t(mDrawIndexCapacity) * sizeof(uint32_t);
	bytes += size_t(mOccludedCapacity) * OccludedClusterBytes;
	bytes += mPostInstanceArgsBuffer ? AssetDispatchWords * sizeof(uint32_t) : 0;
	bytes += mStatisticsBuffer ? StatisticsBufferWords * sizeof(uint32_t) : 0;
	return bytes;
}
}
