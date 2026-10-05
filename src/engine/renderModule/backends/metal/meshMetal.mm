#ifdef METAL_BACKEND
#include "meshMetal.h"

#include <algorithm>
#include <limits>

#include "driverMetal.h"

namespace IKIGAI::RENDER {

	MeshMetal::MeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
		init(vertices, indices, materialIndex);
	}

	MeshMetal::MeshMetal(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t, unsigned materialIndex) {
		init(vertices, indices, materialIndex);
	}

	void MeshMetal::init(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
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
			mVertexBuffer = std::make_shared<VertexBufferMetal>(vertices.size(), sizeof(Vertex));
			mVertexBuffer->setData(vertices.data(), vertices.size(), sizeof(Vertex));
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
			return std::make_shared<MeshMetal>(vertices, indices, offset, materialIndex);
		}
		return std::make_shared<MeshMetal>(vertices, indices, materialIndex);
	}

}
#endif
