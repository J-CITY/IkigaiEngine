#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <serdepp/attribute/default.hpp>
#include <serdepp/serializer.hpp>

#include "coreModule/ecs/components/virtualMeshRenderer.h"
#include "renderModule/backends/interface/gpuFeatures.h"
#include "renderModule/backends/interface/materialInterface.h"
#include "renderModule/backends/interface/vertexLayout.h"
#include "renderModule/frustum.h"
#include "virtualGeometryGpuSelection.h"
#include "virtualGeometrySelection.h"

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::PROFILER {
class GpuStageProfiler;
}

namespace IKIGAI::RENDER {
class Renderer;
class StorageBufferInterface;
class TextureInterface;
class UniformBufferInterface;
class VertexBufferInterface;
struct RenderChunk;
}

namespace IKIGAI::VG {
class VirtualMeshAsset;

// Where LOD selection and cluster culling run. AUTO picks GPU when the backend has compute, indirect dispatch and
// indirect count draws and the selection shaders load; otherwise (and for CPU) the M2 CPU selection runs.
// The environment variable IKIGAI_VG_SELECTION=cpu|gpu|auto overrides the setting (tests).
enum class VirtualGeometrySelectionPath { AUTO = 0, CPU = 1, GPU = 2 };

// Debug modes of the two-phase HZB occlusion (M4-V2). PHASE_COLORS paints cluster albedo by the phase that drew it
// (green: phase 1, red: phase 2). PHASE1_ONLY and UNSAFE_MIP are deliberately non-conservative negative controls of
// the "no false holes" test: phase 2 is skipped, or the HZB is read one mip too fine at a single texel.
// The environment variable IKIGAI_VG_OCCLUSION_DEBUG=none|phase-colors|phase1-only|unsafe-mip overrides the setting.
enum class VirtualGeometryOcclusionDebug { NONE = 0, PHASE_COLORS = 1, PHASE1_ONLY = 2, UNSAFE_MIP = 3 };

// Runtime switches of the virtual geometry renderer (typed descriptor; set by tools, tests and the editor).
struct VirtualGeometrySettings {
	// false draws every virtual mesh through its fallback mesh on the regular path.
	bool Enabled = true;
	// Keeps the LOD cut of each instance from the frame the freeze started; culling still runs.
	bool FreezeLod = false;
	bool FrustumCulling = true;
	bool ConeCulling = true;
	// NONE keeps each component's own DebugMode; another value overrides all instances.
	ECS::VirtualMeshDebugMode DebugOverride = ECS::VirtualMeshDebugMode::NONE;
	// Multiplies every component threshold (quality scaling).
	float ThresholdScale = 1.0f;
	VirtualGeometrySelectionPath SelectionPath = VirtualGeometrySelectionPath::AUTO;
	// Two-phase HZB occlusion on the GPU path. Active only when the pipeline has a VIRTUAL_GEOMETRY_POST stage after an
	// HZB build; otherwise selection is frustum + cone as in M4-V1. IKIGAI_VG_OCCLUSION=on|off overrides it.
	bool OcclusionCulling = true;
	VirtualGeometryOcclusionDebug OcclusionDebug = VirtualGeometryOcclusionDebug::NONE;

	template <class C> static void serde(C& context, VirtualGeometrySettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&VirtualGeometrySettings::Enabled, "Enabled", default_ { true })
			.field(&VirtualGeometrySettings::FreezeLod, "FreezeLod", default_ { false })
			.field(&VirtualGeometrySettings::FrustumCulling, "FrustumCulling", default_ { true })
			.field(&VirtualGeometrySettings::ConeCulling, "ConeCulling", default_ { true })
			.field(&VirtualGeometrySettings::DebugOverride, "DebugOverride", default_ { ECS::VirtualMeshDebugMode::NONE })
			.field(&VirtualGeometrySettings::ThresholdScale, "ThresholdScale", default_ { 1.0f })
			.field(&VirtualGeometrySettings::SelectionPath, "SelectionPath", default_ { VirtualGeometrySelectionPath::AUTO })
			.field(&VirtualGeometrySettings::OcclusionCulling, "OcclusionCulling", default_ { true })
			.field(&VirtualGeometrySettings::OcclusionDebug, "OcclusionDebug", default_ { VirtualGeometryOcclusionDebug::NONE });
	}
};

// Throws std::invalid_argument for out-of-range values (called by setSettings and after deserialization).
void ValidateVirtualGeometrySettings(const VirtualGeometrySettings& settings);

// Per-frame counters. On the GPU path, InstancesCulled, LodClusters, LodTriangles, VisibleClusters, VisibleTriangles,
// FrustumCulledClusters, ConeCulledClusters, LodHistogram and OverflowDraws come from the GPU and are
// GpuStatisticsLatency frames old (1: non-blocking readback of the previous frame); they stay zero
// when GpuStatisticsValid is false. The other fields always describe the current frame.
struct VirtualGeometryFrameStatistics {
	uint32_t Instances = 0;
	uint32_t ClusterInstances = 0;
	uint32_t FallbackInstances = 0;
	uint32_t InstancesCulled = 0;
	uint32_t LodClusters = 0;
	// Triangles of the LOD cut before frustum/cone culling.
	uint32_t LodTriangles = 0;
	uint32_t VisibleClusters = 0;
	uint32_t VisibleTriangles = 0;
	uint32_t FrustumCulledClusters = 0;
	uint32_t ConeCulledClusters = 0;
	uint32_t Batches = 0;
	// drawIndexedIndirect calls recorded by drawGeometry (one per batch unless MaxDrawIndirectCount splits it).
	uint32_t IndirectCalls = 0;
	uint32_t LodHistogram[8] = {};
	double SelectionMilliseconds = 0.0;
	bool GpuSelection = false;
	bool GpuStatisticsValid = false;
	uint32_t GpuStatisticsLatency = 0;
	// Draws dropped because a segment was full (expected 0: segment capacities are proven bounds).
	uint32_t OverflowDraws = 0;
	// Sum of the draw segment capacities of the frame (upper bound of the indirect draws).
	uint32_t DrawCapacity = 0;
	// Bytes of the GPU selection buffers.
	uint64_t GpuSelectionBytes = 0;
	// Two-phase HZB occlusion (M4-V2). OcclusionActive: phase 2 runs this frame; OcclusionHistory: phase 1 tested
	// against the previous HZB. The counters are GPU statistics with the same latency as above.
	bool OcclusionActive = false;
	bool OcclusionHistory = false;
	uint32_t InstancesOccludedPhase1 = 0;
	uint32_t InstancesRevivedPhase2 = 0;
	uint32_t ClustersOccludedPhase1 = 0;
	uint32_t ClustersRevivedPhase2 = 0;
	// Clusters drawn by phase 2 for instances phase 1 rejected as a whole.
	uint32_t ClustersPostInstances = 0;
	// Clusters rejected by the current HZB in phase 2 (finally occluded).
	uint32_t ClustersOccludedPhase2 = 0;
	uint32_t Phase2Triangles = 0;
	uint32_t OccludedListOverflow = 0;
	// Capacity of the occluded-cluster list and CPU time of recordOcclusionPost.
	uint32_t OccludedCapacity = 0;
	double OcclusionPostMilliseconds = 0.0;
};

// Camera inputs for one frame of cluster selection.
struct VirtualGeometryFrameInput {
	VirtualGeometryView View = {};
	uint64_t Frame = 0;
	// The pipeline has an enabled G-buffer stage that calls drawGeometry this frame.
	bool GeometryStageActive = false;
	// Optional GPU timestamps of the selection passes (stages VgInstanceCull and VgClusterCull).
	PROFILER::GpuStageProfiler* GpuProfiler = nullptr;
	// Two-phase HZB occlusion (M4-V2). The pipeline runs an enabled VIRTUAL_GEOMETRY_POST stage this frame (validated
	// at load to follow an HZB build after the Geometry stage): only then may phase 1 reject by occlusion.
	bool OcclusionPostStageActive = false;
	// Temporal history is valid (no camera cut, resize or pipeline reload since the previous frame).
	bool HistoryValid = false;
	// Row-major view-projection with the projection jitter this frame's depth is rendered with, and the previous one.
	MATH::Matrix4f ViewProjection = MATH::Matrix4f::Identity;
	MATH::Matrix4f PreviousViewProjection = MATH::Matrix4f::Identity;
	// Physical HZB texture of the post stage (its size must match the previous frame's for phase 1).
	std::shared_ptr<RENDER::TextureInterface> Hzb = {};
	// Any 2D texture: bound to the HZB slot of the cull shaders when no HZB is tested.
	std::shared_ptr<RENDER::TextureInterface> FallbackTexture = {};
};

// Inputs of the last GPU-selected frame, kept for equivalence tests against the CPU selection.
struct VirtualGeometryGpuFrameRecord {
	struct Instance {
		int OwnerId = 0;
		std::shared_ptr<VirtualMeshAsset> Asset = {};
		// Culling transform (current world) and LOD-test transform (frozen world under FreezeLod).
		VirtualGeometryInstanceTransform Transform = {};
		VirtualGeometryInstanceTransform LodTransform = {};
		// LOD view (Frustum is null: the LOD test does not use it).
		VirtualGeometryView LodView = {};
		float Threshold = 0.0f;
	};
	std::vector<Instance> Instances = {};
	// Culling view of the frame; View.Frustum points to Frustum when HasFrustum.
	RENDER::Frustum Frustum = {};
	bool HasFrustum = false;
	VirtualGeometryView View = {};
	VirtualGeometrySelectionSettings Selection = {};
};

// Non-blocking capture of one GPU-selected frame (tests and debug tools): the inputs (Record; its View.Frustum is
// null, use Record.Frustum) and readback tickets of the results.
struct VirtualGeometryGpuCapture {
	VirtualGeometryGpuFrameRecord Record = {};
	std::vector<VirtualGeometryGpuSegment> Segments = {};
	VirtualGeometryGpuCaptureTickets Tickets = {};
};

// Engine buffers the G-buffer shaders read (owned by GameRenderer).
struct VirtualGeometryDrawBindings {
	std::shared_ptr<RENDER::UniformBufferInterface> EngineUbo = {};
	std::shared_ptr<RENDER::UniformBufferInterface> FrameUbo = {};
	std::shared_ptr<RENDER::TextureInterface> EmptyTexture = {};
};

// GameRenderer helper for VirtualMeshRenderer components (like ParticleRenderer). prepareFrame selects clusters
// on the CPU, uploads indirect arguments and per-draw records, and appends fallback-mesh drawables; drawGeometry
// records one drawIndexedIndirect per (mesh, material slot, material) batch inside the G-buffer stage.
class VirtualGeometryRenderer {
public:
	VirtualGeometryRenderer();
	~VirtualGeometryRenderer();
	VirtualGeometryRenderer(const VirtualGeometryRenderer&) = delete;
	VirtualGeometryRenderer& operator=(const VirtualGeometryRenderer&) = delete;

	// Must run before any stage of the frame (uploads outside render passes). viewChunks receives fallback drawables
	// for instances not drawn as clusters; shadowChunks receives fallback drawables of every shadow-casting instance
	// (shadow maps, ray tracing and GI use the fallback mesh in M2).
	void prepareFrame(RENDER::Renderer& renderer, SCENE_SYSTEM::Scene& scene, const VirtualGeometryFrameInput& input,
		std::vector<RENDER::RenderChunk>& viewChunks, std::vector<RENDER::RenderChunk>& shadowChunks);
	// Records the cluster draws of the frame into the currently bound G-buffer framebuffer (phase 1 with occlusion).
	void drawGeometry(RENDER::Renderer& renderer, const VirtualGeometryDrawBindings& bindings);
	// Two-phase occlusion, phase 2 (VIRTUAL_GEOMETRY_POST stage): outside any render pass, re-tests what phase 1
	// rejected against `hzb` (built from the phase-1 depth). Returns true when post draws may follow.
	bool recordOcclusionPost(RENDER::Renderer& renderer, const std::shared_ptr<RENDER::TextureInterface>& hzb, PROFILER::GpuStageProfiler* profiler);
	// Records the phase-2 draws into the currently bound G-buffer framebuffer (loaded, not cleared).
	void drawOcclusionPost(RENDER::Renderer& renderer, const VirtualGeometryDrawBindings& bindings);
	// Phase 1 of this frame may reject by occlusion, so the post stage must run.
	bool occlusionActive() const;
	// Drops per-frame references (call when the frame or scene ends).
	void endFrame();

	void setSettings(const VirtualGeometrySettings& settings);
	const VirtualGeometrySettings& settings() const;
	const VirtualGeometryFrameStatistics& lastFrame() const;
	// False after the cluster shader or material failed to load; every instance then uses its fallback mesh.
	bool clusterPathAvailable() const;
	// Selection path used by the last prepareFrame.
	VirtualGeometrySelectionPath activeSelectionPath() const;
	// Inputs of the last GPU-selected frame.
	const VirtualGeometryGpuFrameRecord& lastGpuFrame() const;
	// Tests and debug tools: capture the last GPU-selected frame between frames, render at least one more frame,
	// then collect its visible (instance, cluster) pairs (DrawInstance indexes Record.Instances) and statistics.
	// captureGpuSelection returns an empty capture when the last frame was not GPU-selected.
	VirtualGeometryGpuCapture captureGpuSelection(RENDER::Renderer& renderer);
	bool collectGpuSelection(RENDER::Renderer& renderer, const VirtualGeometryGpuCapture& capture,
		std::vector<VirtualGeometryGpuVisibleCluster>& visible, VirtualGeometryGpuStatistics& statistics);

private:
	struct InstanceGpu {
		MATH::Matrix4f World;
		MATH::Matrix4f PreviousWorld;
		MATH::Matrix4f Normal;
		MATH::Vector4f Params; // debug mode, reserved
	};
	static_assert(sizeof(InstanceGpu) == 208);
	struct DrawRecordGpu {
		uint32_t Instance = 0;
		uint32_t Cluster = 0;
		uint32_t LodLevel = 0;
		uint32_t GroupId = 0;
	};
	static_assert(sizeof(DrawRecordGpu) == 16);
	struct BatchKey {
		const VirtualMeshAsset* Asset = nullptr;
		uint32_t Slot = 0;
		const RENDER::MaterialInterface* Material = nullptr;
		bool operator<(const BatchKey& other) const {
			return std::tie(Asset, Slot, Material) < std::tie(other.Asset, other.Slot, other.Material);
		}
	};
	struct Batch {
		std::shared_ptr<VirtualMeshAsset> Asset = {};
		std::shared_ptr<RENDER::MaterialInterface> Material = {};
		uint32_t Slot = 0;
		std::vector<RENDER::DrawIndexedIndirectCommand> Commands = {};
		std::vector<DrawRecordGpu> Records = {};
		uint32_t FirstDraw = 0;
		// CPU path: recorded draws. GPU path: segment capacity (maxDrawCount of drawIndexedIndirectCount).
		uint32_t DrawCount = 0;
		// GPU path: segment index (count buffer slot) and number of instances using the batch.
		uint32_t Segment = 0;
		uint32_t Instances = 0;
	};
	struct FrozenCut {
		const VirtualMeshAsset* Asset = nullptr;
		std::vector<uint32_t> Clusters = {};
	};
	// GPU FreezeLod: the LOD inputs of the frame the freeze started (the cut is a function of them).
	struct FrozenLodView {
		const VirtualMeshAsset* Asset = nullptr;
		VirtualGeometryInstanceTransform Transform = {};
		VirtualGeometryView View = {};
		float Threshold = 0.0f;
	};
	// One cluster-path instance of the frame before selection.
	struct InstanceCandidate {
		ECS::VirtualMeshRenderer* Component = nullptr;
		std::shared_ptr<VirtualMeshAsset> Asset = {};
		MATH::Matrix4f World = MATH::Matrix4f::Identity;
		MATH::Matrix4f PreviousWorld = MATH::Matrix4f::Identity;
		// PreviousWorld is the real pose of the previous frame (not a first frame or a gap).
		bool ContinuousHistory = false;
		VirtualGeometryInstanceTransform Transform = {};
		int OwnerId = 0;
		float Threshold = 0.0f;
		ECS::VirtualMeshDebugMode DebugMode = ECS::VirtualMeshDebugMode::NONE;
	};

	bool ensureClusterMaterial();
	VirtualGeometrySelectionPath resolveSelectionPath(RENDER::Renderer& renderer, bool clusterPath);
	InstanceGpu makeInstanceGpu(const InstanceCandidate& candidate) const;
	Batch& findBatch(const InstanceCandidate& candidate, uint32_t slot);
	void selectOnCpu(const InstanceCandidate& candidate, const VirtualGeometryFrameInput& input, const VirtualGeometrySelectionSettings& selection,
		VirtualGeometrySelectionStatistics& statistics);
	// Builds the GPU tables; returns false (and logs) when a limit of the GPU path is exceeded.
	bool buildGpuFrame(RENDER::Renderer& renderer, const VirtualGeometryFrameInput& input);
	// Decides the two-phase occlusion state of the frame (settings, environment, pipeline, history).
	void resolveOcclusion(const VirtualGeometryFrameInput& input);
	void applyGpuStatistics(const VirtualGeometryGpuStatistics& statistics, uint32_t latency);
	void drawBatches(RENDER::Renderer& renderer, const VirtualGeometryDrawBindings& bindings, bool post);
	bool mGpuFrameRecorded = false;
	void appendFallback(ECS::VirtualMeshRenderer& component, const MATH::Matrix4f& world, const MATH::Matrix4f& previousWorld, int ownerId,
		float distance, std::vector<RENDER::RenderChunk>& chunks) const;
	void uploadFrame(RENDER::Renderer& renderer);
	void copySurfaceUniforms(const RENDER::MaterialInterface& surface);

	VirtualGeometrySettings mSettings = {};
	VirtualGeometryFrameStatistics mStatistics = {};
	bool mMaterialLoadAttempted = false;
	bool mClusterPathAvailable = true;
	std::shared_ptr<RENDER::MaterialInterface> mClusterMaterial = {};
	std::map<std::string, RENDER::MaterialInterface::UniformData> mClusterMaterialDefaults = {};
	RENDER::VertexLayout mLayout = {};

	std::map<BatchKey, Batch> mBatches = {};
	std::vector<InstanceGpu> mInstances = {};
	std::unordered_map<int, FrozenCut> mFrozenCuts = {};
	std::unordered_map<int, FrozenLodView> mFrozenLodViews = {};
	std::vector<InstanceCandidate> mCandidates = {};
	std::vector<uint32_t> mScratchClusters = {};
	std::vector<Batch*> mSlotBatches = {};
	std::vector<RENDER::DrawIndexedIndirectCommand> mCommandScratch = {};
	std::vector<DrawRecordGpu> mRecordScratch = {};
	uint32_t mDrawCount = 0;

	std::shared_ptr<RENDER::StorageBufferInterface> mInstanceBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mDrawRecordBuffer = {};
	std::shared_ptr<RENDER::StorageBufferInterface> mIndirectBuffer = {};
	std::shared_ptr<RENDER::VertexBufferInterface> mDrawIndexStream = {};
	uint32_t mInstanceCapacity = 0;
	uint32_t mDrawCapacity = 0;
	uint32_t mDrawIndexCapacity = 0;
	uint32_t mMaxDrawIndirectCount = 1;

	// GPU selection (M4-V1).
	VirtualGeometryGpuSelection mGpuSelection = {};
	VirtualGeometryGpuFrame mGpuFrame = {};
	VirtualGeometryGpuFrameRecord mGpuFrameRecord = {};
	VirtualGeometrySelectionPath mActivePath = VirtualGeometrySelectionPath::CPU;
	bool mGpuDrawActive = false;
	bool mEnvironmentPathRead = false;
	bool mHasEnvironmentPath = false;
	VirtualGeometrySelectionPath mEnvironmentPath = VirtualGeometrySelectionPath::AUTO;
	std::string mLoggedSelection = {};

	// Two-phase HZB occlusion (M4-V2).
	bool mEnvironmentOcclusionRead = false;
	bool mHasEnvironmentOcclusion = false;
	bool mEnvironmentOcclusion = true;
	bool mHasEnvironmentOcclusionDebug = false;
	VirtualGeometryOcclusionDebug mEnvironmentOcclusionDebug = VirtualGeometryOcclusionDebug::NONE;
	// Phase 1 of the current frame may reject (the post stage must run before the frame ends).
	bool mOcclusionPending = false;
	bool mOcclusionPostDone = false;
	// Set when a frame with phase-1 rejections ended without phase 2: occlusion stays off (logged once).
	bool mOcclusionBroken = false;
	// HZB written by the last frame's pipeline and the view-projection its depth used.
	std::shared_ptr<RENDER::TextureInterface> mHistoryHzb = {};
	MATH::Matrix4f mHistoryViewProjection = MATH::Matrix4f::Identity;
	uint64_t mHistoryFrame = 0;
	bool mHistoryHzbValid = false;
	MATH::Matrix4f mFrameViewProjection = MATH::Matrix4f::Identity;
	uint64_t mFrame = 0;
	std::string mLoggedOcclusion = {};
};
}
