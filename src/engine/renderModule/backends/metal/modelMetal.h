#pragma once
#ifdef METAL_BACKEND
#include "../interface/meshInterface.h"
#include "../interface/modelInterface.h"
#include "bufferMetal.h"
#include "../../vertex.h"
#include <memory>

namespace IKIGAI::RENDER {
    class ModelMetal : public ModelInterface {
        friend class RESOURCES::ModelLoader;
        friend class RESOURCES::AssimpParser;

    public:
        [[nodiscard]] const std::vector<std::shared_ptr<MeshInterface>>& getMeshes() const { return mMeshes; }
        [[nodiscard]] const std::vector<std::string>& getMaterialNames() const { return mMaterialNames; }

        ModelMetal(const std::string& path);
        ~ModelMetal() override = default;

        void createBuffers(const std::vector<Vertex>& p_vertices, const std::vector<uint32_t>& p_indices) override;
        
        std::unique_ptr<VertexBufferMetal> mVertexBuffer;
        std::unique_ptr<IndexBufferMetal> mIndexBuffer;
    };
}
#endif
