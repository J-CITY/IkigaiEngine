#include "materialManager.h"
#include <fstream>
#include "serviceManager.h"
#include <nlohmann/json.hpp>
#include <renderModule/backends/interface/materialInterface.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <renderModule/backends/interface/driverInterface.h>
#include "fileWatcher.h"
#include "utilsModule/jsonLoader.h"
#include "renderModule/render.h"
#include "utilsModule/assertion.h"
#include <utilsModule/pathGetter.h>

namespace IKIGAI::RESOURCES {

	ResourcePtr<RENDER::MaterialInterface> MaterialLoader::Create(const std::string& path, UTILS::IAllocator* allocator, RENDER::MaterialDeleter deleter) {
		auto& render = ServiceManager::Get<RENDER::Renderer>();
		auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
		if (!file || !file->isValid()) {
			ASSERT("MaterialLoader: cannot open .material file");
			return nullptr;
		}

		auto parseResult = UTILS::FromJsonStr<RENDER::MaterialResource>(file->readStr());
		if (parseResult.isErr()) {
			ASSERT("MaterialLoader: failed to parse .material file");
			return nullptr;
		}

		auto materialDescriptor = parseResult.unwrap();
		materialDescriptor.path = path;
		
		return render.createMaterial(materialDescriptor, allocator, deleter);
	}

	ResourcePtr<RENDER::MaterialInterface> MaterialLoader::createFromResource(const std::string& path) {
		if (auto resource = getResource(path)) {
			return resource;
		}

		//TODO fix it
		auto _path = "/" + path;

		RENDER::MaterialResource config;
		if (HasConfig(_path)) {
			config = *GetConfig(_path);
		} else {
			config = LoadConfig(_path);
			AddConfigToCache(_path, config);
		}

		if (auto newResource = Create(_path, nullptr, createCacheDeleter(path))) {
			addFileWatchSubscribe(_path, {_path }, newResource);
			return registerResource(_path, newResource);
		}
		return nullptr;
	}

	ResourcePtr<RENDER::MaterialInterface> MaterialLoader::createResource(const std::string& path) {
		return createFromResource(path);
	}

	ResourcePtr<RENDER::MaterialInterface> MaterialLoader::createResource(const std::string& path, ELoadingType type) {
		return createResource(path, type, std::any());
	}

	ResourcePtr<RENDER::MaterialInterface> MaterialLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
		if (type == ELoadingType::RESOURCE) {
			return createFromResource(path);
		}
		// MEMORY and FILE not supported for Material directly in this context without JSON
		return createFromResource(path);
	}

	bool MaterialLoader::reloadResource(std::weak_ptr<RENDER::MaterialInterface> weakRes, const std::string& path) {
		if (auto mat = weakRes.lock()) {
			auto config = LoadConfig(path);
			AddConfigToCache(path, config);
			mat->create(config);
			addFileWatchSubscribe(path, {path}, weakRes);
			return true;
		}
		return false;
	}

}
