#include "modelManager.h"
#include "serviceManager.h"
#include <resourceModule/fileSystem/fileSystem.h>
#include <renderModule/backends/interface/modelInterface.h>

#include "utilsModule/pathGetter.h"
#include "renderModule/render.h"
#include "fileWatcher.h"

#include "coreModule/ecs/components/transform.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include <renderModule/backends/interface/driverInterface.h>
#include "utilsModule/assertion.h"

namespace IKIGAI::RESOURCES {

	AssimpParser ModelLoader::_ASSIMP;

	void ModelLoader::Reload(RENDER::ModelInterface& model, const RENDER::ModelResource& config) {
		auto usedFlags = ModelParserFlags::NONE;
		for (auto e : config.flags) {
			usedFlags |= e;
		}
		std::shared_ptr<RENDER::ModelInterface> newModel = Create(config.pathModel, usedFlags);

		if (newModel) {
			model.setMeshes(newModel->getMeshes());
			model.setMaterialNames(newModel->getMaterialsNames());
			newModel->clearMeshes();
		}
	}

	//TODO: remove
	ResourcePtr<IKIGAI::RENDER::ModelInterface> ModelLoader::CreateFromFile(const std::string& path, ModelParserFlags parserFlags, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
		std::string realPath = "/" + path;
		auto model = Create("/" + path,
			parserFlags == ModelParserFlags::NONE ? getDefaultFlag() : parserFlags, allocator, deleter);
		if (model) {
			model->setPath(path);
		}
		return model;
	}

	ResourcePtr<IKIGAI::RENDER::ModelInterface> ModelLoader::CreateFromResource(const std::string& path, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
		auto config = LoadConfig(path);
		return CreateFromResource(config, allocator, deleter);
	}

	ResourcePtr<IKIGAI::RENDER::ModelInterface> ModelLoader::CreateFromResource(const RENDER::ModelResource& config, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
		ModelParserFlags parserFlags = ModelParserFlags::NONE;
		for (auto e : config.flags) {
			parserFlags |= e;
		}
		// Грузим сам файл модели
		auto model = CreateFromFile(config.pathModel, parserFlags, allocator, deleter);
		return model;
	}



	ResourcePtr<RENDER::ModelInterface> ModelLoader::Create(const std::string& filepath, ModelParserFlags parserFlags, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
		auto& render = ServiceManager::Get<RENDER::Renderer>();
		auto result = render.createModel(filepath, allocator, deleter);
		if (!result) {
			ASSERT("ModelLoader::Create: Failed to allocate model object");
			return nullptr;
		}

		auto data = ServiceManager::Get<FileSystem>().getFile(filepath)->read();
		if (data.empty()) {
			ASSERT("Can`t create model");
			return nullptr;
		}

		if (_ASSIMP.LoadModel(filepath, data, result, parserFlags)) {
			result->computeBoundingSphere();
			return result;
		}
		ASSERT("Can`t create model");
		return nullptr;
	}

	ResourcePtr<RENDER::ModelInterface> ModelLoader::CreateVerts(const std::string& filepath, ModelParserFlags parserFlags,
		std::vector<std::vector<Vertex>>& _globalVerticesPerMesh,
		std::vector<std::vector<uint32_t>>& _globalIndicesPerMesh) {

		auto& render = ServiceManager::Get<RENDER::Renderer>();
		auto result = render.createModel(filepath);
		if (!result) {
			ASSERT("ModelLoader::CreateVerts: Failed to allocate model object");
			return nullptr;
		}
		auto data = ServiceManager::Get<FileSystem>().getFile(filepath)->read();
		if (data.empty())
			return nullptr;

		if (_ASSIMP.LoadVertexes(filepath, data, result, parserFlags,
			_globalVerticesPerMesh, _globalIndicesPerMesh)) {
			result->computeBoundingSphere();
			return result;
		}
		return nullptr;
	}

	ModelParserFlags ModelLoader::getDefaultFlag() {
		ModelParserFlags flags = ModelParserFlags::TRIANGULATE;
		flags |= ModelParserFlags::GEN_SMOOTH_NORMALS;
		flags |= ModelParserFlags::FLIP_UVS;
		flags |= ModelParserFlags::GEN_UV_COORDS;
		flags |= ModelParserFlags::CALC_TANGENT_SPACE;
		//flags |= ModelParserFlags::JOIN_IDENTICAL_VERTICES;
		//flags |= ModelParserFlags::IMPROVE_CACHE_LOCALITY;
		//flags |= ModelParserFlags::FIND_INVALID_DATA;
		//flags |= ModelParserFlags::FIND_INSTANCES;
		//flags |= ModelParserFlags::OPTIMIZE_MESHES;
		//flags |= ModelParserFlags::OPTIMIZE_GRAPH;
		//flags |= ModelParserFlags::DEBONE;
		// flags |= IKIGAI::RESOURCES::ModelParserFlags::PRE_TRANSFORM_VERTICES;
		// flags |= IKIGAI::RESOURCES::ModelParserFlags::FLIP_WINDING_ORDER;
		return flags;
	}

	ResourcePtr<RENDER::ModelInterface> ModelLoader::createResource(const std::string& path) {
		// Если файл .model — грузим через дескриптор-ресурс
		ResourcePtr<RENDER::ModelInterface> res;
		if (path.ends_with(".model")) {
			return createResource(path, ELoadingType::RESOURCE);
		}
		// Иначе грузим напрямую через файл
		return createResource(path, ELoadingType::FILE);
	}

	ResourcePtr<RENDER::ModelInterface> ModelLoader::createResource(const std::string& path, ELoadingType type) {
		return createResource(path, type, std::any());
	}

	ResourcePtr<RENDER::ModelInterface> ModelLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
		ResourcePtr<RENDER::ModelInterface> res;
		std::unordered_set<std::string> paths = {path};
		if (type == ELoadingType::FILE) {
			res = CreateFromFile(path, ModelParserFlags::NONE, nullptr, createCacheDeleter(path));
		}
		else if (type == ELoadingType::RESOURCE) {
			RENDER::ModelResource config;
			if (HasConfig(path)) {
				config = *GetConfig(path);
			}
			else {
				config = LoadConfig(path);
				AddConfigToCache(path, config);
			}
			paths.insert(config.pathModel);
			res = CreateFromResource(config, nullptr, createCacheDeleter(path));
		}
		else {
			// TODO: MEMORY/DESCRIPTOR if needed
			res = CreateFromFile(path, ModelParserFlags::NONE, nullptr, createCacheDeleter(path));
		}

		{
			addFileWatchSubscribe(path, paths, res);
		}

		return res;
	}

	bool ModelLoader::reloadResource(std::weak_ptr<RENDER::ModelInterface> weakRes, const std::string& path) {
		if (auto res = weakRes.lock()) {
			std::unordered_set<std::string> paths = {path};
			RENDER::ModelResource config;
			if (path.ends_with(".model")) {
				config = LoadConfig(path);
				AddConfigToCache(path, config);
				paths.insert(config.pathModel);
			} else {
				config.path = path;
				config.pathModel = path;
			}

			Reload(*res, config);
			addFileWatchSubscribe(path, paths, weakRes);
			return true;
		}
		return false;
	}
}
