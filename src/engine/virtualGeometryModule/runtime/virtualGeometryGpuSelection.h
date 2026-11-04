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
};
static_assert(sizeof(VirtualGeometryCullInstanceGpu) == 176);

// Flags of VirtualGeometryCullInstanceGpu.
enum VirtualGeometryCullInstanceFlags : uint32_t {
	VG_CULL_INSTANCE_CONE_CULLING = 1u << 0,
	VG_CULL_INSTANCE_LOD_ORTHOGRAPHIC = 1u << 1,
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
};
static_assert(sizeof(VirtualGeometryCullViewGpu) == 144);

// One asset of the frame: its instances are contiguous in the instance table.
struct VirtualGeometryGpuAsset {
	std::shared_ptr<VirtualMeshAsset> Asset = {};
	uint32_t FirstInstance = 0;
	uint32_t InstanceCount = 0;
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
	uint32_t DrawCapacity = 0;
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
};

// GPU LOD selection and culling of the virtual geometry renderer (M4-V1). Owns the selection buffers and the two
// compute shaders; record() uploads the frame tables and records, outside any render pass:
//   clear counters -> vgInstanceCull (all instances) -> vgClusterCull per asset (dispatchIndirect) -> statistics copy.
// The draw pass then issues one drawIndexedIndirectCount per segment with the segment counter as count.
class VirtualGeometryGpuSelection {
public:
	VirtualGeometryGpuSelection();
	~VirtualGeometryGpuSelection();
	VirtualGeometryGpuSelection(const VirtualGeometryGpuSelection&) = delete;
	VirtualGeometryGpuSelection& operator=(const VirtualGeometryGpuSelection&) = delete;

	// Empty when the GPU path can run on this backend, otherwise the reason (missing feature or shader).
	std::string unavailableReason(RENDER::Renderer& renderer);
	// Uploads the frame, records the selection passes and requests a non-blocking readback of the statistics,
	// collected by takeStatistics in the next frame.
	void record(RENDER::Renderer& renderer, const VirtualGeometryGpuFrame& frame, PROFILER::GpuStageProfiler* profiler);
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
	VirtualGeometryGpuStatistics decodeStatistics(const std::vector<uint8_t>& bytes) const;

	bool mShadersLoaded = false;
	bool mShaderLoadAttempted = false;
	bool mAsyncReadback = true;
	std::shared_ptr<RENDER::ShaderInterface> mInstanceCullShader = {};
	std::shared_ptr<RENDER::ShaderInterface> mClusterCullShader = {};
	std::shared_ptr<RENDER::UniformBufferInterface> mViewUniforms = {};

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
	uint32_t mInstanceCapacity = 0;
	uint32_t mAssetCapacity = 0;
	uint32_t mSlotBatchCapacity = 0;
	uint32_t mSegmentCapacity = 0;
	uint32_t mDrawCapacity = 0;
	uint32_t mDrawIndexCapacity = 0;

	std::vector<uint32_t> mAssetDispatchScratch = {};
	std::vector<uint32_t> mSegmentScratch = {};
	std::vector<uint32_t> mZeroScratch = {};
	uint64_t mPendingTicket = 0;
	bool mHasPendingTicket = false;
};
}
