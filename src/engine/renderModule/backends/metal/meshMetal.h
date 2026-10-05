#pragma once
#ifdef METAL_BACKEND

#include <memory>
#include <vector>

#include "../../vertex.h"
#include "../interface/meshInterface.h"
#include "bufferMetal.h"

namespace IKIGAI::RENDER {
	class MeshMetal : public MeshInterface {
	public:
		MeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex);
		MeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex);
		~MeshMetal() override = default;

		void bind() const override;
		void unbind() const override;

	private:
		void init(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex);
		void createBuffers(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices);
		void computeBoundingSphere(const std::vector<Vertex>& vertices);

		std::shared_ptr<VertexBufferMetal> mVertexBuffer;
		std::shared_ptr<IndexBufferMetal> mIndexBuffer;
	};

	std::shared_ptr<MeshInterface> CreateMeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex);
}

#endif
