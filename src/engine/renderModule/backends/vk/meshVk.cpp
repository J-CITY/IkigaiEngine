#include "meshVk.h"

#ifdef VULKAN_BACKEND
#include <algorithm>
#include <limits>

#include "driverVk.h"
#include "helpers.h"
#include "vertexBufferVk.h"
#include "indexBufferVk.h"

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

MeshVk::MeshVk(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
	init(vertices, indices, materialIndex);
}

MeshVk::MeshVk(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t /*offset*/, unsigned materialIndex) {
	init(vertices, indices, materialIndex);
}

MeshVk::~MeshVk() = default;

void MeshVk::init(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
	mVertexCount = vertices.size();
	mIndicesCount = indices.size();
	mMaterialIndex = materialIndex;
	mOffset = 0;

	createBuffers(vertices, indices);
	computeBoundingSphere(vertices);
}

void MeshVk::bind() const {
	auto* driver = UtilityVk::GetDriver();
	if (mVertexBuffer) {
		driver->setVertexBuffer(mVertexBuffer);
	}
	if (mIndexBuffer && mIndicesCount > 0) {
		driver->setIndexBuffer(mIndexBuffer);
	}
}

void MeshVk::unbind() const {
}

void MeshVk::createBuffers(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices) {
	if (!vertices.empty()) {
		mVertexBuffer = std::make_shared<VertexBufferVk>(vertices);
	}
	if (!indices.empty()) {
		mIndexBuffer = std::make_shared<IndexBufferVk>(indices);
	}
}

void MeshVk::computeBoundingSphere(const std::vector<Vertex>& vertices) {
	mBoundingSphere.position = MATH::Vector3f::Zero;
	mBoundingSphere.radius = 0.0f;

	if (!vertices.empty()) {
		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float minZ = std::numeric_limits<float>::max();

		// lowest(), not min(): min() is the smallest positive value and breaks all-negative meshes
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

		mBoundingSphere.position = MATH::Vector3f{ minX + maxX, minY + maxY, minZ + maxZ } / 2.0f;

		for (const auto& vertex : vertices) {
			const auto& position = reinterpret_cast<const MATH::Vector3f&>(vertex.position);
			mBoundingSphere.radius = std::max(mBoundingSphere.radius, MATH::Vector3f::Distance(mBoundingSphere.position, position));
		}
	}
}

#endif
