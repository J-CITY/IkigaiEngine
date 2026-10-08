#pragma once
#include <cstdint>

#ifdef OPENGL_BACKEND
#include <memory>
#include <span>
#include <optional>

#include "../../vertex.h"
#include "vertexBufferGl.h"
#include "../interface/meshInterface.h"


namespace IKIGAI
{
	namespace RENDER
	{
		class IndexBufferGl;
		struct BoundingSphere;
		class IndexBuffer;
	}
}

namespace IKIGAI {
	namespace RENDER {
		class MeshGl: public MeshInterface {
		public:

			template<class Vertex>
			MeshGl(const VertexDescriptor& descriptor, const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset = 0, unsigned materialIndex = 0):
				MeshGl(descriptor, vertices.data(), sizeof(Vertex), vertices.size(), indices, offset, materialIndex) {

			}

			template<class Vertex>
			MeshGl(const VertexDescriptor& descriptor, const std::vector<Vertex>& vertices, size_t offset = 0, unsigned materialIndex = 0):
				MeshGl(descriptor, vertices.data(), sizeof(Vertex), vertices.size(), {}, offset, materialIndex) {

			}

			MeshGl(const VertexDescriptor& descriptor, void* vertices, size_t vertexSize, size_t count, std::vector<unsigned> indices, size_t offset, unsigned materialIndex);


			MeshGl(std::vector<Vertex> vertices, std::vector<unsigned> indices, unsigned materialIndex);
			MeshGl(std::vector<Vertex> vertices, std::vector<unsigned> indices, size_t offset, unsigned materialIndex);
			virtual ~MeshGl();
			virtual void bind() const override;
			virtual void unbind() const override;
			//virtual size_t getVertexCount() const override;
			//virtual size_t getIndexCount() const override;
			//uint32_t getMaterialIndex() const override;
		private:
			void createBuffers(std::vector<Vertex> p_vertices, std::vector<uint32_t> p_indices);
			void computeBoundingSphere(std::span<Vertex> vertices);
		public:
			VertexDescriptor mDescriptor;
			size_t mVertexSize = 0;
			//unsigned int mVertexCount = 0;
			//const unsigned int mIndicesCount = 0;
			//const unsigned int mMaterialIndex = 0;
//#ifndef USING_GLES
			std::unique_ptr<VertexArray> mVertexArray;
//#endif
			std::unique_ptr<VertexBufferGl> mVertexBuffer;
			std::unique_ptr<IndexBufferGl> mIndexBuffer;

			//std::optional<size_t> mOffset = std::nullopt;
		};
	}
}
#endif
