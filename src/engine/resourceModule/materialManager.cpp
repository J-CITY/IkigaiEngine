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
	auto candidate = BeginConfigLoad(path);
	if (!candidate.descriptor) {
		auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Missing material: " + path);
		}
		auto bytes = co_await file->readAllTask(control);
		ParseConfigCandidate(candidate, std::string(bytes.begin(), bytes.end()), path);
	}
	const auto& descriptor = *candidate.descriptor;
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
	if (!material) {
		throw std::runtime_error("Cannot create material: " + path);
	}
	if (!PublishConfig(path, candidate)) {
		throw ResourceLoadCancelled();
	}
	auto published = registerResource(path, std::move(material), control.get());
	addFileWatchSubscribe(path, { path }, published);
	co_return published;
}

ResourcePtr<RENDER::MaterialInterface> MaterialLoader::Create(
	const std::string& path, UTILS::IAllocator* allocator, RENDER::MaterialDeleter deleter) {
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	return render.createMaterial(LoadConfig(path), allocator, deleter);
}

ResourcePtr<RENDER::MaterialInterface> MaterialLoader::createFromResource(const std::string& path) {
	if (auto resource = getResource(path)) {
		return resource;
	}

	const auto key = NormalizePath(path);
	const auto candidate = ReadConfigCandidate(key);
	auto& renderer = ServiceManager::Get<RENDER::Renderer>();
	if (auto resource = renderer.createMaterial(*candidate.descriptor, nullptr, createCacheDeleter(key)); resource && PublishConfig(key, candidate)) {
		addFileWatchSubscribe(key, { key }, resource);
		return registerResource(key, resource);
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
		const auto candidate = ReadConfigCandidate(path, true);
		auto replacement = ServiceManager::Get<RENDER::Renderer>().createMaterial(*candidate.descriptor);
		if (!replacement || !commitResourceReload(path, mat, path, candidate, [&]() noexcept { return mat->adoptReloadedMaterial(*replacement); })) {
			return false;
		}
		addFileWatchSubscribe(path, { path }, mat);
		return true;
	}
	return false;
}

}
