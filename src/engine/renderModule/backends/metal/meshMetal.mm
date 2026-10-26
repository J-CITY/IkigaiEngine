#include "utilsModule/memory/resourceAllocator.h"
#ifdef METAL_BACKEND
#include "meshMetal.h"

#include <algorithm>
#include <limits>

#include "driverMetal.h"
#include "metalVertexLayout.h"
#include "../interface/universalMesh.h"

namespace IKIGAI::RENDER {
	std::shared_ptr<MeshInterface> CreateMeshMetal(const MeshData& data, unsigned materialIndex) {
		class CustomMeshMetal final : public UniversalMesh {
		public:
			CustomMeshMetal(const MeshData& data,unsigned materialIndex) : UniversalMesh(data,materialIndex) {
				for (const auto& stream : data.streams) {
					auto buffer = std::make_shared<VertexBufferMetal>(stream.count,mLayout.stream(stream.binding).stride);
					buffer->setData(stream.bytes.data(),stream.count,buffer->getStride());
					mStreams.emplace_back(stream.binding,buffer);
				}
				if (!data.indices.empty()) {
					auto buffer = std::make_shared<IndexBufferMetal>(data.indices.size(),sizeof(uint32_t));
					buffer->setData(data.indices.data(),data.indices.size(),sizeof(uint32_t)); mIndexBuffer = buffer;
				}
			}
		};
		return IKIGAI::MEMORY::MakeShared<CustomMeshMetal, IKIGAI::MEMORY::Category::Mesh>(data,materialIndex);
	}

	MeshMetal::MeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
		init(vertices, indices, materialIndex);
	}

	MeshMetal::MeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t, unsigned materialIndex) {
		init(vertices, indices, materialIndex);
	}

	void MeshMetal::init(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
		mCanonicalVertices = vertices;
		mCanonicalIndices = indices;
		mVertexCount = vertices.size();
		mIndicesCount = indices.size();
		mMaterialIndex = materialIndex;
		mOffset = 0;
		createBuffers(vertices, indices);
		computeBoundingSphere(vertices);
	}

	void MeshMetal::bind() const {
		auto* driver = static_cast<DriverMetal*>(DriverInterface::Get());
		if (!driver) {
			return;
		}
		if (mVertexBuffer) {
			driver->setVertexBuffer(mVertexBuffer);
		}
		if (mIndexBuffer && mIndicesCount > 0) {
			driver->setIndexBuffer(mIndexBuffer);
		}
	}

	void MeshMetal::unbind() const {}

	void MeshMetal::createBuffers(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices) {
		if (!vertices.empty()) {
			std::vector<MetalMeshVertex> packed(vertices.size());
			for (size_t i = 0; i < vertices.size(); ++i) {
				const Vertex& vertex = vertices[i];
				MetalMeshVertex& out = packed[i];
				out.position[0] = vertex.position.x;
				out.position[1] = vertex.position.y;
				out.position[2] = vertex.position.z;
				out.texCoord[0] = vertex.texCoord.x;
				out.texCoord[1] = vertex.texCoord.y;
				out.normal[0] = vertex.normal.x;
				out.normal[1] = vertex.normal.y;
				out.normal[2] = vertex.normal.z;
				out.tangent[0] = vertex.tangent.x;
				out.tangent[1] = vertex.tangent.y;
				out.tangent[2] = vertex.tangent.z;
				out.bitangent[0] = vertex.bitangent.x;
				out.bitangent[1] = vertex.bitangent.y;
				out.bitangent[2] = vertex.bitangent.z;
				for (unsigned bone = 0; bone < 4; ++bone) {
					out.boneIds[bone] = vertex.m_BoneIDs[bone];
					out.weights[bone] = vertex.m_Weights[bone];
				}
			}
			mVertexBuffer = std::make_shared<VertexBufferMetal>(packed.size(), sizeof(MetalMeshVertex));
			mVertexBuffer->setData(packed.data(), packed.size(), sizeof(MetalMeshVertex));
		}
		if (!indices.empty()) {
			mIndexBuffer = std::make_shared<IndexBufferMetal>(indices.size(), sizeof(unsigned));
			mIndexBuffer->setData(indices.data(), indices.size(), sizeof(unsigned));
		}
	}

	void MeshMetal::computeBoundingSphere(const std::vector<Vertex>& vertices) {
		mBoundingSphere.position = MATH::Vector3f::Zero;
		mBoundingSphere.radius = 0.0f;
		if (vertices.empty()) {
			return;
		}

		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float minZ = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float maxY = std::numeric_limits<float>::lowest();
		float maxZ = std::numeric_limits<float>::lowest();
		for (const auto& vertex : vertices) {
			minX = std::min(minX, vertex.position.x);
			minY = std::min(minY, vertex.position.y);
			minZ = std::min(minZ, vertex.position.z);
			maxX = std::max(maxX, vertex.position.x);
			maxY = std::max(maxY, vertex.position.y);
			maxZ = std::max(maxZ, vertex.position.z);
		}
		mBoundingSphere.position = MATH::Vector3f{minX + maxX, minY + maxY, minZ + maxZ} / 2.0f;
		for (const auto& vertex : vertices) {
			mBoundingSphere.radius = std::max(mBoundingSphere.radius, MATH::Vector3f::Distance(mBoundingSphere.position, vertex.position));
		}
	}

	std::shared_ptr<MeshInterface> CreateMeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex) {
		if (offset != 0) {
			return IKIGAI::MEMORY::MakeShared<MeshMetal, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, offset, materialIndex);
		}
		return IKIGAI::MEMORY::MakeShared<MeshMetal, IKIGAI::MEMORY::Category::Mesh>(vertices, indices, materialIndex);
	}

}
#endif
