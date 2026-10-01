#pragma once
#include <memory>
#include <string>
#include <vector>
#include "renderModule/vertex.h"

namespace IKIGAI::RENDER {
	class ModelInterface;
	class MeshInterface;

	std::shared_ptr<ModelInterface> CreateEmptyModel(const std::string& path = "");
	std::shared_ptr<MeshInterface> CreateMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex = 0);
	std::shared_ptr<MeshInterface> CreateMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex);
}
