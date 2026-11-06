#include "meshVk.h"

#ifdef VULKAN_BACKEND
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
    if(UtilityVk::GetDriver()->mRayTracingCapabilities.AccelerationStructure) {
        mRayTracingVertices=vertices;mRayTracingIndices=indices;
    }
	if (!vertices.empty()) {
		mVertexBuffer = std::make_shared<VertexBufferVk>(vertices);
	}
	if (!indices.empty()) {
		mIndexBuffer = std::make_shared<IndexBufferVk>(indices);
	}
}

#endif
