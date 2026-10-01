#pragma once
#ifdef OPENGL_BACKEND

#include "indexBufferGl.h"
#include "vertexBufferGl.h"
#include <memory>
#include <string>
#include <vector>

#include "../interface/meshInterface.h"
#include "../interface/modelInterface.h"

namespace IKIGAI
{
	class Vertex;
}

namespace IKIGAI {

	namespace RENDER {

		class ModelGl: public ModelInterface {
			friend class RESOURCES::ModelLoader;
			friend class RESOURCES::AssimpParser;

		public:
			//[[nodiscard]] const std::vector<std::shared_ptr<MeshInterface>>& getMeshes() const;
			//[[nodiscard]] const std::vector<std::string>& getMaterialNames() const;
			ModelGl(const std::string& p_path);
			ModelGl(const std::string& path, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
			~ModelGl() override;

		public:
			// If all meshes in one buffer
			void createBuffers(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) override;
			std::unique_ptr<VertexArray> vertexArray;
			std::unique_ptr<VertexBufferGl> vertexBuffer;
			std::unique_ptr<IndexBufferGl> indexBuffer;
		};
	}
}
#endif

