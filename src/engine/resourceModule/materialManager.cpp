#include "materialManager.h"
#include "fileWatcher.h"
#include "renderModule/render.h"
#include "serviceManager.h"
#include "shaderManager.h"
#include "textureManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <renderModule/backends/interface/driverInterface.h>
#include <renderModule/backends/interface/materialInterface.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <utilsModule/pathGetter.h>

namespace IKIGAI::RESOURCES {

COROUTINE::Task<ResourcePtr<RENDER::MaterialInterface>> MaterialLoader::createResourceTask(
	std::string path, ELoadingType, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing material: " + path);
	}
	auto bytes = co_await file->readAllTask(control);
	auto parsed = UTILS::FromJsonStr<RENDER::MaterialResource>(std::string(bytes.begin(), bytes.end()));
	if (parsed.isErr()) {
		throw std::runtime_error("Invalid material: " + path);
	}
	auto descriptor = parsed.unwrap();
	descriptor.path = path;
	descriptor.PbrLayers.validate(path);
	std::vector<std::shared_ptr<void>> dependencies;
	dependencies.push_back(co_await ServiceManager::Get<ShaderLoader>().loadResourceTask(descriptor.ShaderPath));
	for (const auto& [name, value] : descriptor.Uniforms) {
		control->check();
		if (const auto texture = std::get_if<std::string>(&value); texture && !texture->empty()) {
			dependencies.push_back(co_await ServiceManager::Get<TextureLoader>().loadResourceTask(
				*texture, texture->ends_with(".texture") ? ELoadingType::RESOURCE : ELoadingType::FILE));
		}
	}
	for (const auto& layer : descriptor.PbrLayers.Layers) {
		for (const auto* texture : { &layer.AlbedoMap, &layer.NormalMap, &layer.OrmMap, &layer.HeightMap, &layer.MaskMap }) {
			control->check();
			if (texture->empty()) {
				continue;
			}
			if (!ServiceManager::Get<FileSystem>().isFileExist(*texture)) {
				throw std::runtime_error("Missing PbrLayers texture in " + path + ": " + *texture);
			}
			dependencies.push_back(co_await ServiceManager::Get<TextureLoader>().loadResourceTask(
				*texture, texture->ends_with(".texture") ? ELoadingType::RESOURCE : ELoadingType::FILE));
		}
	}
	control->check();
	co_await COROUTINE::NextFrame();
	control->check();
	auto material = ServiceManager::Get<RENDER::Renderer>().createMaterial(descriptor, nullptr, createCacheDeleter(path));
	addFileWatchSubscribe(path, { path }, material);
	co_return registerResource(path, material);
}

ResourcePtr<RENDER::MaterialInterface> MaterialLoader::Create(
	const std::string& path, UTILS::IAllocator* allocator, RENDER::MaterialDeleter deleter) {
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

	// TODO fix it
	auto _path = NormalizePath(path);

	RENDER::MaterialResource config;
	if (HasConfig(_path)) {
		config = *GetConfig(_path);
	}
	else {
		config = LoadConfig(_path);
		AddConfigToCache(_path, config);
	}

	if (auto newResource = Create(_path, nullptr, createCacheDeleter(path))) {
		addFileWatchSubscribe(_path, { _path }, newResource);
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
		addFileWatchSubscribe(path, { path }, weakRes);
		return true;
	}
	return false;
}

}
