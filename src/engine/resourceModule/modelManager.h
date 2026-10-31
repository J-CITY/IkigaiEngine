#pragma once
#include <string>

#include "parser/assimpParser.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "resourceManager.h"
#include "utilsModule/event.h"
#include "utilsModule/memory/memory.h"


namespace IKIGAI {
namespace RENDER {
	class ModelInterface;
}
}

namespace IKIGAI {
namespace RESOURCES {
	class ModelLoader : public ResourceManager<RENDER::ModelInterface, RENDER::ModelResource> {
	public:
		static void Reload(RENDER::ModelInterface& model, const RENDER::ModelResource& config);
		static ResourcePtr<RENDER::ModelInterface> CreateFromResource(
			const std::string& path, UTILS::IAllocator* allocator = nullptr, RENDER::ModelDeleter deleter = nullptr);
		static ResourcePtr<RENDER::ModelInterface> CreateFromResource(
			const RENDER::ModelResource& config, UTILS::IAllocator* allocator = nullptr, RENDER::ModelDeleter deleter = nullptr);
		static ResourcePtr<RENDER::ModelInterface> CreateFromFile(
			const std::string& path, ModelParserFlags parserFlags, UTILS::IAllocator* allocator = nullptr, RENDER::ModelDeleter deleter = nullptr);
		// move to private
		static ResourcePtr<RENDER::ModelInterface> Create(const std::string& filepath, ModelParserFlags parserFlags = ModelParserFlags::NONE,
			UTILS::IAllocator* allocator = nullptr, RENDER::ModelDeleter deleter = nullptr);
		static ResourcePtr<IKIGAI::RENDER::ModelInterface> CreateVerts(const std::string& filepath, ModelParserFlags parserFlags,
			std::vector<std::vector<Vertex>>& _globalVerticesPerMesh, std::vector<std::vector<uint32_t>>& _globalIndicesPerMesh);
		static ModelParserFlags getDefaultFlag();

	private:
		COROUTINE::Task<ResourcePtr<RENDER::ModelInterface>> createResourceTask(
			std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) override;

		ResourcePtr<RENDER::ModelInterface> createResource(const std::string& path) override;
		ResourcePtr<RENDER::ModelInterface> createResource(const std::string& path, ELoadingType type) override;
		ResourcePtr<RENDER::ModelInterface> createResource(const std::string& path, ELoadingType type, std::any data) override;

		bool reloadResource(std::weak_ptr<RENDER::ModelInterface> weakRes, const std::string& path) override;
	};

}
}
