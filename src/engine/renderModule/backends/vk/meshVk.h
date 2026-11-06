#pragma once
#include "renderModule/vertex.h"

#ifdef VULKAN_BACKEND

#include <volk.h>

#include <memory>
#include <span>
#include <vector>

#include "vertexBufferVk.h"
#include "indexBufferVk.h"
#include "../interface/meshInterface.h"

namespace IKIGAI::RENDER {
	class MeshVk : public MeshInterface {
	public:
		MeshVk(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex);
		// Vulkan backend does not support batching yet: every mesh owns its buffers, so offset is not used.
		MeshVk(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex);
		~MeshVk() override;

		void bind() const override;
		void unbind() const override;
        const std::vector<Vertex>& rayTracingVertices() const override {return mRayTracingVertices;}
        const std::vector<unsigned>& rayTracingIndices() const override {return mRayTracingIndices;}
        std::shared_ptr<IndexBufferInterface> rayTracingIndexBuffer() const override {return mIndexBuffer;}

		// Counts, material index and offset are stored in MeshInterface (do not shadow them here).
		std::shared_ptr<VertexBufferVk> mVertexBuffer;
		std::shared_ptr<IndexBufferVk> mIndexBuffer;
        std::vector<Vertex> mRayTracingVertices;
        std::vector<unsigned> mRayTracingIndices;

	private:
		void init(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex);
		void createBuffers(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices);
	};
}
#endif
