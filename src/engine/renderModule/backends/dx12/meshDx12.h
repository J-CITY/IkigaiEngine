#pragma once
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/vertex.h"
#ifdef DX12_BACKEND
#include <memory>
#include <vector>

#include "indexBufferDx12.h"
#include "vertexBufferDx12.h"

namespace IKIGAI::RENDER {
class Renderer;
// TODO: add support offset in buffers
class MeshDx12 : public MeshInterface {
public:
	struct GeometryRange {
		uint32_t IndexCount = 0;
		uint32_t FirstIndex = 0;
		int32_t BaseVertex = 0;
	};
	struct GeometryPool {
		std::shared_ptr<MeshInterface> Mesh;
		std::vector<GeometryRange> Ranges;
	};
	static bool SupportsGeometryPool(const MeshInterface& mesh);
	static GeometryPool CreateGeometryPool(Renderer& renderer, std::span<const std::shared_ptr<MeshInterface>> meshes);

	MeshDx12(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex = 0);
	MeshDx12(const std::vector<Vertex>& vertices, unsigned materialIndex = 0);
	void bind() const override;
	;
	void unbind() const override;
	;
	const std::vector<Vertex>& rayTracingVertices() const override {
		return mRayVertices;
	}
	const std::vector<unsigned>& rayTracingIndices() const override {
		return mRayIndices;
	}
	std::shared_ptr<IndexBufferInterface> rayTracingIndexBuffer() const override {
		return mIndexBuffer;
	}

private:
	std::vector<Vertex> mRayVertices;
	std::vector<unsigned> mRayIndices;
	std::shared_ptr<VertexBufferInterface> mVertexBuffer;
	std::shared_ptr<IndexBufferInterface> mIndexBuffer;
};
}
#endif
