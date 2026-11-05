#pragma once

#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "resourceManager.h"
#include "utilsModule/event.h"
#include <string>

namespace IKIGAI {
namespace RENDER {
	class ShaderInterface;
}
}

namespace IKIGAI {
namespace RESOURCES {
	class ShaderLoader : public ResourceManager<RENDER::ShaderInterface, RENDER::ShaderResource> {
	public:
		ResourcePtr<RENDER::ShaderInterface> loadMaterialShader(const std::string& path, unsigned additionalLayers);
		static RENDER::RayTracingProgramResource LoadProgramResource(const std::string& filePath);
		static std::unordered_set<std::string> ResolveSources(RENDER::ShaderResource& resource);
		static ResourcePtr<RENDER::ShaderInterface> Create(
			const std::string& filePath, UTILS::IAllocator* allocator = nullptr, RENDER::ShaderDeleter deleter = nullptr);
		static ResourcePtr<RENDER::ShaderInterface> CreateFromResource(
			const RENDER::ShaderResource& res, UTILS::IAllocator* allocator = nullptr, RENDER::ShaderDeleter deleter = nullptr);

		static ResourcePtr<RENDER::ShaderInterface> CreateFromSource(const std::string& vertexShader, const std::string& fragmentShader,
			const std::string& geometryShader = "", const std::string& tessCompShader = "", const std::string& tessEvoluationShader = "",
			const std::string& computeShader = "", UTILS::IAllocator* allocator = nullptr, RENDER::ShaderDeleter deleter = nullptr);
		static void Recompile(RENDER::ShaderInterface& shader, const RENDER::ShaderResource& res);

	protected:
		COROUTINE::Task<ResourcePtr<RENDER::ShaderInterface>> createResourceTask(
			std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) override;
		virtual std::shared_ptr<RENDER::ShaderInterface> createResource(const std::string& path) override;
		virtual std::shared_ptr<RENDER::ShaderInterface> createResource(const std::string& path, ELoadingType type) override;
		virtual std::shared_ptr<RENDER::ShaderInterface> createResource(const std::string& path, ELoadingType type, std::any data) override;

		bool reloadResource(std::weak_ptr<RENDER::ShaderInterface> weakRes, const std::string& path) override;
		void addFileWatchSubscribe(
			const std::string& path, const std::unordered_set<std::string>& previous, std::weak_ptr<RENDER::ShaderInterface> shader) override;
	};
}
}
