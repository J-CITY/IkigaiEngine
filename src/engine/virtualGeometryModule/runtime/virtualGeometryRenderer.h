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
#include "virtualGeometrySelection.h"

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
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

	template <class C> static void serde(C& context, VirtualGeometrySettings& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&VirtualGeometrySettings::Enabled, "Enabled", default_ { true })
			.field(&VirtualGeometrySettings::FreezeLod, "FreezeLod", default_ { false })
			.field(&VirtualGeometrySettings::FrustumCulling, "FrustumCulling", default_ { true })
			.field(&VirtualGeometrySettings::ConeCulling, "ConeCulling", default_ { true })
			.field(&VirtualGeometrySettings::DebugOverride, "DebugOverride", default_ { ECS::VirtualMeshDebugMode::NONE })
			.field(&VirtualGeometrySettings::ThresholdScale, "ThresholdScale", default_ { 1.0f });
	}
};

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
};

// Camera inputs for one frame of cluster selection.
struct VirtualGeometryFrameInput {
	VirtualGeometryView View = {};
	uint64_t Frame = 0;
	// The pipeline has an enabled G-buffer stage that calls drawGeometry this frame.
	bool GeometryStageActive = false;
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
	// Records the cluster draws of the frame into the currently bound G-buffer framebuffer.
	void drawGeometry(RENDER::Renderer& renderer, const VirtualGeometryDrawBindings& bindings);
	// Drops per-frame references (call when the frame or scene ends).
	void endFrame();

	void setSettings(const VirtualGeometrySettings& settings);
	const VirtualGeometrySettings& settings() const;
	const VirtualGeometryFrameStatistics& lastFrame() const;
	// False after the cluster shader or material failed to load; every instance then uses its fallback mesh.
	bool clusterPathAvailable() const;

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
		uint32_t DrawCount = 0;
	};
	struct FrozenCut {
		const VirtualMeshAsset* Asset = nullptr;
		std::vector<uint32_t> Clusters = {};
	};

	bool ensureClusterMaterial();
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
};
}
