#include "meshDx12.h"
#ifdef DX12_BACKEND

#include "vertexBufferDx12.h"
#include "indexBufferDx12.h"
#include "driverDx12.h"

IKIGAI::RENDER::MeshDx12::MeshDx12(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
    if(d3dUtil::GetDriver()->rayTracingCapabilities().AccelerationStructure){mRayVertices=vertices;mRayIndices=indices;}
	mVertexBuffer = std::make_shared<VertexBufferDx12>(vertices);
	mIndexBuffer = std::make_shared<IndexBufferDx12>(indices);
	mIndicesCount = mIndexBuffer->getSize();
	mVertexCount = mVertexBuffer->getSize();
	mMaterialIndex = materialIndex;
	computeBoundingSphere(vertices);
}

IKIGAI::RENDER::MeshDx12::MeshDx12(const std::vector<Vertex>& vertices, unsigned materialIndex) {
    if(d3dUtil::GetDriver()->rayTracingCapabilities().AccelerationStructure)mRayVertices=vertices;
	mVertexBuffer = std::make_shared<VertexBufferDx12>(vertices);
	mVertexCount = mVertexBuffer->getSize();
	mMaterialIndex = materialIndex;
	computeBoundingSphere(vertices);
}

void IKIGAI::RENDER::MeshDx12::bind() const {
	mVertexBuffer->bind();
	if (mIndexBuffer) {
		mIndexBuffer->bind();
	}
}

void IKIGAI::RENDER::MeshDx12::unbind() const {
	mVertexBuffer->unbind();
	if (mIndexBuffer) {
		mIndexBuffer->unbind();
	}
}
#endif
