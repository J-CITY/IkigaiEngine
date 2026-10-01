#include "meshFactory.h"
#include "interface/driverInterface.h"

#ifdef OPENGL_BACKEND
#include "gl/modelGl.h"
#include "gl/meshGl.h"
#endif
#ifdef VULKAN_BACKEND
#include "vk/modelVk.h"
#include "vk/meshVk.h"
#endif
#ifdef DX12_BACKEND
#include "dx12/modelDx12.h"
#include "dx12/meshDx12.h"
#endif
#ifdef METAL_BACKEND
namespace IKIGAI::RENDER {
	std::shared_ptr<ModelInterface> CreateEmptyModelMetal(const std::string& path);
}
#endif

namespace IKIGAI::RENDER {
	std::shared_ptr<ModelInterface> CreateEmptyModel(const std::string& path) {
		switch (DriverInterface::settings.backend) {
#ifdef OPENGL_BACKEND
		case RenderSettings::Backend::OPENGL:
			return std::make_shared<ModelGl>(path);
#endif
#ifdef VULKAN_BACKEND
		case RenderSettings::Backend::VULKAN:
			return std::make_shared<ModelVk>(path);
#endif
#ifdef DX12_BACKEND
		case RenderSettings::Backend::DIRECTX12:
			return std::make_shared<ModelDx12>(path);
#endif
#ifdef METAL_BACKEND
		case RenderSettings::Backend::METAL:
			return CreateEmptyModelMetal(path);
#endif
		default:
			return nullptr;
		}
	}

	std::shared_ptr<MeshInterface> CreateMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, unsigned materialIndex) {
		return CreateMesh(vertices, indices, static_cast<size_t>(0), materialIndex);
	}

	std::shared_ptr<MeshInterface> CreateMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, size_t offset, unsigned materialIndex) {
		switch (DriverInterface::settings.backend) {
#ifdef OPENGL_BACKEND
		case RenderSettings::Backend::OPENGL:
			if (offset != 0) {
				return std::make_shared<MeshGl>(vertices, indices, offset, materialIndex);
			}
			return std::make_shared<MeshGl>(vertices, indices, materialIndex);
#endif
#ifdef VULKAN_BACKEND
		case RenderSettings::Backend::VULKAN:
			if (offset != 0) {
				return std::make_shared<MeshVk>(vertices, indices, offset, materialIndex);
			}
			return std::make_shared<MeshVk>(vertices, indices, materialIndex);
#endif
#ifdef DX12_BACKEND
		case RenderSettings::Backend::DIRECTX12:
			return std::make_shared<MeshDx12>(vertices, indices, materialIndex);
#endif
		default:
			return nullptr;
		}
	}
}
