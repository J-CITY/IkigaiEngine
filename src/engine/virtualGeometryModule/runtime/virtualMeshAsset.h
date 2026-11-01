#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "virtualGeometryModule/format/vmeshIO.h"

namespace IKIGAI::RENDER {
class IndexBufferInterface;
class ModelInterface;
class StorageBufferInterface;
class VertexBufferInterface;
}

namespace IKIGAI::VG {
// GPU copies of a cooked virtual mesh used by the cluster renderer (Vulkan and DirectX 12 only).
struct VirtualMeshGpuBuffers {
	// VMeshVertex[VertexCount], bound as vertex stream 0.
	std::shared_ptr<RENDER::VertexBufferInterface> Vertices = {};
	// uint32_t[IndexCount]: every cluster of every LOD level plus the fallback ranges.
	std::shared_ptr<RENDER::IndexBufferInterface> Indices = {};
	// VMeshCluster[ClusterCount], unused by the M2 CPU selection; reserved for GPU culling (M4).
	std::shared_ptr<RENDER::StorageBufferInterface> Clusters = {};
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
	VirtualMeshAsset(std::string path, const VMeshData& data, bool createClusterBuffers);
	~VirtualMeshAsset();
	VirtualMeshAsset(const VirtualMeshAsset&) = delete;
	VirtualMeshAsset& operator=(const VirtualMeshAsset&) = delete;

	const std::string& path() const;
	const VMeshHeader& header() const;
	const std::vector<VMeshCluster>& clusters() const;
	uint32_t materialSlotCount() const;
	uint32_t leafTriangleCount() const;
	uint32_t fallbackTriangleCount() const;
	bool hasClusterBuffers() const;
	const VirtualMeshGpuBuffers& gpuBuffers() const;
	const std::shared_ptr<RENDER::ModelInterface>& fallbackModel() const;
	// Bytes of the cluster renderer buffers (0 without them).
	size_t gpuBytes() const;

private:
	void createFallbackModel(const VMeshData& data);
	void createClusterBuffers(const VMeshData& data);

	std::string mPath = {};
	VMeshHeader mHeader = {};
	std::vector<VMeshCluster> mClusters = {};
	uint32_t mLeafTriangleCount = 0;
	uint32_t mFallbackTriangleCount = 0;
	VirtualMeshGpuBuffers mGpu = {};
	size_t mGpuBytes = 0;
	std::shared_ptr<RENDER::ModelInterface> mFallbackModel = {};
};
}
