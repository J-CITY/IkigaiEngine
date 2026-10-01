#ifdef METAL_BACKEND
#include "modelMetal.h"
#include "driverMetal.h"
#include <resourceModule/serviceManager.h>

namespace IKIGAI::RENDER {

    ModelMetal::ModelMetal(const std::string& path) {
        mPath = path;
    }

    std::shared_ptr<ModelInterface> CreateEmptyModelMetal(const std::string& path) {
        return std::make_shared<ModelMetal>(path);
    }

    void ModelMetal::createBuffers(const std::vector<Vertex>& p_vertices, const std::vector<uint32_t>& p_indices) {
        auto& driver = static_cast<DriverMetal&>(RESOURCES::ServiceManager::Get<DriverInterface>());
        id<MTLDevice> device = driver.getDevice();
        
        mVertexBuffer = std::make_unique<VertexBufferMetal>(device, p_vertices.size(), sizeof(Vertex));
        mVertexBuffer->setData(p_vertices.data(), p_vertices.size(), sizeof(Vertex));
        
        mIndexBuffer = std::make_unique<IndexBufferMetal>(device, p_indices.size(), sizeof(uint32_t));
        mIndexBuffer->setData(p_indices.data(), p_indices.size(), sizeof(uint32_t));
    }

}
#endif
