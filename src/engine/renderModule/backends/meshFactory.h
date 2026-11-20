#pragma once
#include "interface/vertexLayout.h"
#include "renderModule/objects/boundingSphere.h"
#include "renderModule/vertex.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace IKIGAI::RENDER {
class ModelInterface;
class MeshInterface;
class IndexBufferInterface;
class VertexBufferInterface;
struct MeshData {
	VertexLayout layout;
	std::vector<VertexStreamData> streams;
	std::span<const uint32_t> indices;
	size_t vertexCount = 0;
	// Bounds are explicit: arbitrary vertex bytes need not contain a position.
	BoundingSphere bounds;
	// Optional canonical geometry for the existing RT/skinning scene builder.
	std::span<const Vertex> rayTracingVertices;
};
std::shared_ptr<MeshInterface> CreateMesh(const MeshData& data, unsigned materialIndex = 0);

// Input-assembly buffers for GPU-driven geometry on Vulkan and DirectX 12.
std::shared_ptr<VertexBufferInterface> CreateGpuDrivenVertexBuffer(const void* data, size_t count, size_t stride);
std::shared_ptr<IndexBufferInterface> CreateGpuDrivenIndexBuffer(const uint32_t* data, size_t count);

struct StreamingMeshBufferSlot {
	std::shared_ptr<VertexBufferInterface> Vertices {};
	std::shared_ptr<IndexBufferInterface> Indices {};
};

// Creates updateable slots during native frame recording. Each index buffer has
// room for indexCapacity 32-bit indices.
std::vector<StreamingMeshBufferSlot> CreateStreamingMeshBufferSlots(
	size_t slotCount, size_t vertexCapacity, size_t vertexStride, size_t indexCapacity);

std::shared_ptr<ModelInterface> CreateEmptyModel(const std::string& path = "");
std::shared_ptr<MeshInterface> CreateMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex = 0);
std::shared_ptr<MeshInterface> CreateMesh(
	const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex);
}
