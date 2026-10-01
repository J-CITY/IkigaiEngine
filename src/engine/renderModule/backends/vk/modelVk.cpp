#include "modelVk.h"

#ifdef VULKAN_BACKEND

using namespace IKIGAI;
using namespace IKIGAI::RENDER;

#include "indexBufferVk.h"

ModelVk::ModelVk(const std::string& path) {
	mPath = path;
}

ModelVk::~ModelVk() = default;

const std::vector<std::shared_ptr<MeshInterface>>& ModelVk::getMeshes() const {
	return mMeshes;
}

const std::vector<std::string>& ModelVk::getMaterialNames() const {
	return mMaterialNames;
}

void ModelVk::createBuffers(const std::vector<Vertex>& p_vertices, const std::vector<uint32_t>& p_indices) {
	mVertexBuffer = std::make_unique<VertexBufferVk>(p_vertices);
	mIndexBuffer = std::make_unique<IndexBufferVk>(p_indices);
}


#endif
