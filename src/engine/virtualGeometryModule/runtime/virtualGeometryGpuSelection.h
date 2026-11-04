#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderModule/backends/interface/gpuFeatures.h"

namespace IKIGAI::PROFILER {
class GpuStageProfiler;
}

namespace IKIGAI::RENDER {
class Renderer;
class ShaderInterface;
class StorageBufferInterface;
class TextureInterface;
class UniformBufferInterface;
class VertexBufferInterface;
}

namespace IKIGAI::VG {
class VirtualMeshAsset;

// One cluster instance as read by vgInstanceCull / vgClusterCull (std430, mirrors VgCullInstance in vgCullCommon.glsl).
struct VirtualGeometryCullInstanceGpu {
	// Rows of the current world matrix (row-major 3x4, culling).
	float WorldRows[3][4] = {};
	// Rows of the world matrix of the LOD test (the frozen one under FreezeLod).
	float LodRows[3][4] = {};
	// Asset bounding sphere in object space.
	float Bounds[4] = {};
	// xyz LOD camera position, w projection scale.
	float LodCamera[4] = {};
	// x threshold, y max scale of the world matrix, z max scale of the LOD matrix, w LOD zNear.
	float Params[4] = {};
	uint32_t AssetDispatch = 0;
	uint32_t VisibleSegment = 0;
	uint32_t SlotBatchBase = 0;
	uint32_t Flags = 0;
	uint32_t DrawInstance = 0;
	uint32_t ClusterCount = 0;
	uint32_t SlotCount = 0;
	uint32_t Reserved = 0;
	// Rows of the previous-frame world matrix (phase-1 occlusion against the previous HZB, M4-V2).
	float PreviousRows[3][4] = {};
	// x max axis scale of the previous world matrix, yzw reserved.
	float PreviousParams[4] = {};
};
static_assert(sizeof(VirtualGeometryCullInstanceGpu) == 240);

// Flags of VirtualGeometryCullInstanceGpu.
enum VirtualGeometryCullInstanceFlags : uint32_t {
	VG_CULL_INSTANCE_CONE_CULLING = 1u << 0,
	VG_CULL_INSTANCE_LOD_ORTHOGRAPHIC = 1u << 1,
	// Continuous transform history and a valid previous HZB: phase 1 may reject the instance by occlusion.
	VG_CULL_INSTANCE_OCCLUSION_HISTORY = 1u << 2,
};

// Pass modes of VirtualGeometryCullViewGpu::PassMode (VG_PASS_* in vgCullCommon.glsl).
enum class VirtualGeometryCullPass : uint32_t { MAIN = 0, POST_INSTANCES = 1, POST_CLUSTERS = 2 };

// Debug flags of the occlusion test (VG_OCCLUSION_DEBUG_* in vgCullCommon.glsl; deliberately non-conservative).
enum VirtualGeometryOcclusionDebugFlags : uint32_t {
	VG_OCCLUSION_DEBUG_SKIP_POST = 1u << 0,
	VG_OCCLUSION_DEBUG_UNSAFE_MIP = 1u << 1,
};

// Per-dispatch constants (std140 VgCullView).
struct VirtualGeometryCullViewGpu {
	float Planes[6][4] = {};
	float Camera[4] = {};
	uint32_t FrustumCulling = 0;
	uint32_t InstanceCount = 0;
	uint32_t FlagsReserved0 = 0;
	uint32_t FlagsReserved1 = 0;
	uint32_t AssetDispatch = 0;
	uint32_t ClusterCount = 0;
	uint32_t VisibleSegment = 0;
	uint32_t AssetReserved = 0;
	uint32_t PassMode = 0;
	uint32_t AssetCount = 0;
	uint32_t OccludedBase = 0;
	uint32_t OccludedCapacity = 0;
	uint32_t OcclusionTest = 0;
	uint32_t SegmentOffset = 0;
	uint32_t OcclusionDebug = 0;
	uint32_t OcclusionReserved = 0;
	// Rows of the view-projection the HZB of this pass was rendered with.
	float OcclusionViewProjection[4][4] = {};
};
static_assert(sizeof(VirtualGeometryCullViewGpu) == 240);

// One asset of the frame: its instances are contiguous in the instance table.
struct VirtualGeometryGpuAsset {
	std::shared_ptr<VirtualMeshAsset> Asset = {};
	uint32_t FirstInstance = 0;
	uint32_t InstanceCount = 0;
	// Segment of the occluded-cluster list (instances x sum of the per-slot cut bounds).
	uint32_t OccludedBase = 0;
	uint32_t OccludedCapacity = 0;
};

// Two-phase HZB occlusion inputs of one frame (M4-V2).
struct VirtualGeometryGpuOcclusion {
	// The pipeline runs the post pass this frame (phase 2): phase 1 may reject, phase 2 re-tests.
	bool Active = false;
	// Phase 1 tests instances with history against PreviousHzb (false: first frame, cut, resize, no history).
	bool History = false;
	uint32_t DebugFlags = 0;
	// Row-major jittered view-projection of the previous frame (PreviousHzb) and of this frame (current HZB).
	float PreviousViewProjection[4][4] = {};
	float ViewProjection[4][4] = {};
	std::shared_ptr<RENDER::TextureInterface> PreviousHzb = {};
	// Bound to the HZB slot of the cull shaders when no HZB is tested (descriptors must be valid).
	std::shared_ptr<RENDER::TextureInterface> FallbackTexture = {};
	// Sum of the asset occluded-list capacities.
	uint32_t OccludedCapacity = 0;
};

// Draw segment of one (mesh, slot, material) batch in the indirect command buffer.
struct VirtualGeometryGpuSegment {
	uint32_t FirstDraw = 0;
	uint32_t Capacity = 0;
};

// CPU tables of one frame of GPU selection, built by VirtualGeometryRenderer.
struct VirtualGeometryGpuFrame {
	std::vector<VirtualGeometryCullInstanceGpu> Instances = {};
	std::vector<uint32_t> SlotBatches = {};
	std::vector<VirtualGeometryGpuSegment> Segments = {};
	std::vector<VirtualGeometryGpuAsset> Assets = {};
	VirtualGeometryCullViewGpu View = {};
	// Draws of the phase-1 segments. With occlusion the post segments follow: segment s + Segments.size(),
	// draws [DrawCapacity, 2 * DrawCapacity).
	uint32_t DrawCapacity = 0;
	VirtualGeometryGpuOcclusion Occlusion = {};
};

// Counters written by the cluster/instance passes (word order of VG_STAT_* in vgCullCommon.glsl).
struct VirtualGeometryGpuStatistics {
	uint32_t VisibleInstances = 0;
	uint32_t InstancesCulled = 0;
	uint32_t LodClusters = 0;
	uint32_t LodTriangles = 0;
	uint32_t VisibleClusters = 0;
	uint32_t VisibleTriangles = 0;
	uint32_t FrustumCulled = 0;
	uint32_t ConeCulled = 0;
	uint32_t Overflow = 0;
	uint32_t LodHistogram[8] = {};
	// Two-phase occlusion (M4-V2); VisibleClusters/VisibleTriangles above include both phases.
	uint32_t InstancesOccludedMain = 0;
	uint32_t InstancesRevived = 0;
	uint32_t ClustersOccludedMain = 0;
	uint32_t ClustersRevived = 0;
	uint32_t ClustersPostInstances = 0;
	uint32_t ClustersOccludedPost = 0;
	uint32_t PostTriangles = 0;
	uint32_t OccludedListOverflow = 0;
};

// One visible (draw instance, cluster) pair read back from the draw records (tests and debug tools).
struct VirtualGeometryGpuVisibleCluster {
	uint32_t DrawInstance = 0;
	uint32_t Cluster = 0;
};

// Non-blocking readback tickets of one recorded frame (segment counters, draw records, statistics).
struct VirtualGeometryGpuCaptureTickets {
	uint64_t Counts = 0;
	uint64_t Records = 0;
	uint64_t Statistics = 0;
	uint32_t RecordBytes = 0;
	// With occlusion: post segment s is counted at s + PostSegmentOffset and starts PostDrawOffset draws later.
	uint32_t PostSegmentOffset = 0;
	uint32_t PostDrawOffset = 0;
};

// GPU LOD selection and culling of the virtual geometry renderer (M4-V1). Owns the selection buffers and the two
// compute shaders; record() uploads the frame tables and records, outside any render pass:
//   clear counters -> vgInstanceCull (all instances) -> vgClusterCull per asset (dispatchIndirect) -> statistics copy.
// The draw pass then issues one drawIndexedIndirectCount per segment with the segment counter as count.
// M4-V2 two-phase HZB occlusion (frame.Occlusion.Active): record() is phase 1 (previous HZB); what it rejects by
// occlusion is listed, and recordPost() re-tests the lists against the HZB of the phase-1 depth:
//   vgInstanceCullPost -> vgClusterCull (post instances) per asset -> vgClusterCullPost (listed clusters) per asset.
// Phase-2 draws go to post segments (own counters and draw ranges) drawn by a second pass into the same G-buffer.
class VirtualGeometryGpuSelection {
public:
	VirtualGeometryGpuSelection();
	~VirtualGeometryGpuSelection();
	VirtualGeometryGpuSelection(const VirtualGeometryGpuSelection&) = delete;
	VirtualGeometryGpuSelection& operator=(const VirtualGeometryGpuSelection&) = delete;

	// Empty when the GPU path can run on this backend, otherwise the reason (missing feature or shader).
	std::string unavailableReason(RENDER::Renderer& renderer);
	// Uploads the frame, records the selection passes and requests a non-blocking readback of the statistics,
	// collected by takeStatistics in the next frame. With frame.Occlusion.Active this is phase 1 and the statistics
	// readback waits for recordPost.
	void record(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame, PROFILER::GpuStageProfiler* profiler);
	// Phase 2 (M4-V2), outside any render pass, after the HZB of the phase-1 depth was built: re-tests what phase 1
	// rejected against `hzb` and fills the post segments. Then requests the statistics readback.
	void recordPost(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame, const std::shared_ptr<RENDER::TextureInterface>& hzb,
		PROFILER::GpuStageProfiler* profiler);
	// Statistics of the previously recorded frame: false when none are available (no asynchronous readback on this
	// backend, or the readback is still pending).
	bool takeStatistics(RENDER::Renderer& renderer, VirtualGeometryGpuStatistics& statistics);
	// Tests and debug tools: copies the selection results of the frame recorded latest into readback memory
	// (non-blocking; call between frames, before the next record). collectCapture succeeds once the GPU finished.
	// No blocking readBuffer is used: its mid-frame flush trips Vulkan synchronization validation after presents.
	VirtualGeometryGpuCaptureTickets requestCapture(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame);
	bool collectCapture(RENDER::Renderer& renderer, const VirtualGeometryGpuCaptureTickets& tickets,
		const std::vector<VirtualGeometryGpuSegment>& segments, std::vector<VirtualGeometryGpuVisibleCluster>& visible,
		VirtualGeometryGpuStatistics& statistics);

	const std::shared_ptr<RENDER::StorageBufferInterface>& indirectBuffer() const;
	const std::shared_ptr<RENDER::StorageBufferInterface>& drawRecordBuffer() const;
	const std::shared_ptr<RENDER::StorageBufferInterface>& segmentCountBuffer() const;
	const std::shared_ptr<RENDER::VertexBufferInterface>& drawIndexStream() const;
	// Bytes of the selection buffers currently allocated.
	size_t gpuBytes() const;

private:
	bool loadShaders();
	void ensureCapacity(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame);
	void setViewUniforms(RENDER::Renderer& renderer, const VirtualGeometryCullViewGpu& view);
	void bindClusterCullBuffers(RENDER::Renderer& renderer, const std::shared_ptr<RENDER::StorageBufferInterface>& visibleInstances);
	void requestStatisticsReadback(RENDER::Renderer& renderer);
	VirtualGeometryGpuStatistics decodeStatistics(const std::vector<uint8_t>& bytes) const;

	bool mShadersLoaded = false;
	bool mShaderLoadAttempted = false;
	bool mAsyncReadback = true;
	std::shared_ptr<RENDER::ShaderInterface> mInstanceCullShader = {};
	std::shared_ptr<RENDER::ShaderInterface> mClusterCullShader = {};
	std::shared_ptr<RENDER::ShaderInterface> mInstanceCullPostShader = {};
	std::shared_ptr<RENDER::ShaderInterface> mClusterCullPostShader = {};
	// One uniform buffer per dispatch of the frame: a buffer updated between dispatches of one command list would
	// rely on the backend versioning setData (V1 did); separate buffers make the ordering explicit.
	std::vector<std::shared_ptr<RENDER::UniformBufferInterface>> mViewUniformPool = {};
	size_t mViewUniformsUsed = 0;

	std::shared_ptr<RENDER::StorageBufferInterface> mCullInstanceBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mVisibleInstanceBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mAssetDispatchBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mSlotBatchBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mSegmentBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mSegmentCountBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mIndirectBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mDrawRecordBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mStatisticsBuffer = {};
	std::shared_ptr<RENDER::VertexBufferInterface> mDrawIndexStream = {};
	// Two-phase occlusion (M4-V2). Indirect arguments live in separate buffers so that no buffer is bound for
	// shader writes while it is the argument buffer of the same dispatch (DirectX 12 resource states).
	std::shared_ptr<RENDER::StorageBufferInterface> mOccludedInstanceBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mPostInstanceArgsBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mPostInstanceDispatchBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mPostVisibleInstanceBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mOccludedClusterBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mOccludedClusterCountBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mPostClusterArgsBuffer = {};
	uint32_t mInstanceCapacity = 0;
	uint32_t mAssetCapacity = 0;
	uint32_t mSlotBatchCapacity = 0;
	uint32_t mSegmentCapacity = 0;
	uint32_t mDrawCapacity = 0;
	uint32_t mDrawIndexCapacity = 0;
	uint32_t mOccludedCapacity = 0;

	std::vector<uint32_t> mAssetDispatchScratch = {};
	std::vector<uint32_t> mPostDispatchScratch = {};
	std::vector<uint32_t> mSegmentScratch = {};
	std::vector<uint32_t> mZeroScratch = {};
	uint64_t mPendingTicket = 0;
	bool mHasPendingTicket = false;
};
}
