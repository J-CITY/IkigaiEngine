#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "virtualGeometryHierarchy.h"
#include "virtualGeometryModule/format/vmeshIO.h"
#include "virtualGeometrySelection.h"

namespace IKIGAI::RENDER {
class IndexBufferInterface;
class ModelInterface;
class StorageBufferInterface;
class VertexBufferInterface;
}

namespace IKIGAI::VG {
class VirtualMeshPageSource;
// GPU copies of a cooked virtual mesh used by the cluster renderer (Vulkan and DirectX 12 only).
struct VirtualMeshGpuBuffers {
	// VMeshVertex[VertexCount], bound as vertex stream 0.
	std::shared_ptr<RENDER::VertexBufferInterface> Vertices = {};
	// uint32_t[IndexCount]: every cluster of every LOD level plus the fallback ranges.
	std::shared_ptr<RENDER::IndexBufferInterface> Indices = {};
	// VMeshCluster[ClusterCount], read directly by the GPU selection (vgClusterCull.comp.glsl).
	std::shared_ptr<RENDER::StorageBufferInterface> Clusters = {};
	std::shared_ptr<RENDER::StorageBufferInterface> HierarchyNodes = {};
	std::shared_ptr<RENDER::StorageBufferInterface> HierarchyClusters = {};
	// The visibility opt-in mirrors the resident IA data at load time.
	std::shared_ptr<RENDER::StorageBufferInterface> ResolveVertices = {};
	std::shared_ptr<RENDER::StorageBufferInterface> ResolveIndices = {};
};

// True when the active backend can draw clusters: Vulkan or DirectX 12 with indirect draws and buffer usage flags.
bool IsVirtualGeometryGpuPathSupported();
// Plain vertex buffer on the active Vulkan/DirectX 12 backend (throws elsewhere). Used for the cluster vertices
// and for the per-draw index stream of the cluster renderer.
std::shared_ptr<RENDER::VertexBufferInterface> CreateVirtualGeometryVertexBuffer(const void* data, size_t count, size_t stride);

// Immutable runtime form of a .vmesh file. Owns the CPU cluster table used for LOD selection, the GPU buffers
// used by the cluster renderer and a regular ModelInterface built from the fallback ranges (shadows, ray tracing,
// backends without indirect draws). Shared between all components that reference the same file.
class VirtualMeshAsset {
public:
	// Validated data from ReadVMesh. createClusterBuffers is false on backends without the GPU path; the
	// fallback model is always created.
	VirtualMeshAsset(std::string path, const VMeshData& data, bool createClusterBuffers, std::shared_ptr<const VirtualMeshPageSource> pageSource = {},
		bool visibilityBuffers = false);
	VirtualMeshAsset(std::string path, std::shared_ptr<const VirtualMeshPageSource> source);
	~VirtualMeshAsset();
	VirtualMeshAsset(const VirtualMeshAsset&) = delete;
	VirtualMeshAsset& operator=(const VirtualMeshAsset&) = delete;

	const std::string& path() const;
	const std::shared_ptr<const VirtualMeshPageSource>& pageSource() const;
	const VMeshHeader& header() const;
	const std::vector<VMeshCluster>& clusters() const;
	const VirtualGeometryHierarchy& hierarchy() const;
	uint32_t materialSlotCount() const;
	uint32_t leafTriangleCount() const;
	uint32_t fallbackTriangleCount() const;
	bool hasClusterBuffers() const;
	bool streamsPages() const;
	const VirtualMeshGpuBuffers& gpuBuffers() const;
	const std::shared_ptr<RENDER::ModelInterface>& fallbackModel() const;
	// Bytes of the cluster renderer buffers (0 without them).
	size_t gpuBytes() const;
	// Per-slot bound of the clusters one instance contributes to any LOD cut (computed and checked at load).
	const VirtualGeometrySlotCutBounds& cutBounds() const;

private:
	void createFallbackModel(const VMeshData& data);
	void createClusterBuffers(const VMeshData& data, bool visibilityBuffers);

	std::string mPath = {};
	std::shared_ptr<const VirtualMeshPageSource> mPageSource = {};
	VMeshHeader mHeader = {};
	std::vector<VMeshCluster> mClusters = {};
	VirtualGeometryHierarchy mHierarchy = {};
	bool mStreamsPages = false;
	uint32_t mLeafTriangleCount = 0;
	uint32_t mFallbackTriangleCount = 0;
	VirtualMeshGpuBuffers mGpu = {};
	size_t mGpuBytes = 0;
	VirtualGeometrySlotCutBounds mCutBounds = {};
	std::shared_ptr<RENDER::ModelInterface> mFallbackModel = {};
};
}
