#pragma once
#include "renderModule/vertex.h"

#ifdef VULKAN_BACKEND

#include <volk.h>

#include <memory>
#include <span>

#include <memory>
#include <span>
#include <optional>

#include "vertexBufferVk.h"
#include "../interface/meshInterface.h"

namespace IKIGAI
{
	namespace RENDER
	{
		class IndexBufferVk;
		struct BoundingSphere;
		class IndexBuffer;
	}
}

namespace IKIGAI::RENDER {
	class MeshVk : public MeshInterface {
	public:
		MeshVk(std::vector<Vertex> p_vertices, std::vector<unsigned> indices, unsigned materialIndex);
		MeshVk(std::vector<Vertex> vertices, std::vector<unsigned> indices, size_t offset, unsigned materialIndex);
		virtual ~MeshVk();
		virtual void bind() const{};
		virtual void unbind() const;
		virtual size_t getVertexCount() const;
		virtual size_t getIndexCount() const;
	private:
		void createBuffers(std::vector<Vertex> p_vertices, std::vector<uint32_t> p_indices);
		void computeBoundingSphere(std::vector<Vertex> vertices);

	public:

		const unsigned int mVertexCount = 0;
		const unsigned int mIndicesCount = 0;
		const unsigned int mMaterialIndex = 0;
		
		std::unique_ptr<VertexBufferVk> mVertexBuffer;
		std::unique_ptr<IndexBufferVk> mIndexBuffer;

		std::optional<size_t> mOffset = std::nullopt;
	};
}
#endif
