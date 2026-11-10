#include "modelManager.h"
#include "serviceManager.h"
#include "utilsModule/time/loadTimer.h"
#include <renderModule/backends/interface/modelInterface.h>
#include <resourceModule/fileSystem/fileSystem.h>

#include "fileWatcher.h"
#include "renderModule/render.h"
#include "taskModule/taskSystem.h"
#include "utilsModule/pathGetter.h"

#include "coreModule/ecs/components/transform.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "utilsModule/assertion.h"
#include <renderModule/backends/interface/driverInterface.h>

namespace IKIGAI::RESOURCES {

COROUTINE::Task<ResourcePtr<RENDER::ModelInterface>> ModelLoader::createResourceTask(
	std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
	UTILS::LoadTimer timing("model", path);
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	auto source = path;
	auto flags = getDefaultFlag();
	auto& fs = ServiceManager::Get<FileSystem>();
	ConfigCandidate candidate;
	if (path.ends_with(".model") && type == ELoadingType::RESOURCE) {
		candidate = BeginConfigLoad(path);
		if (!candidate.descriptor) {
			auto file = fs.getFile(path, FileMode::READ);
			if (!file || !file->isValid()) {
				throw std::runtime_error("Missing model descriptor: " + path);
			}
			auto bytes = co_await file->readAllTask(control);
			ParseConfigCandidate(candidate, std::string(bytes.begin(), bytes.end()), path);
		}
		const auto& config = *candidate.descriptor;
		source = config.pathModel;
		flags = ModelParserFlags::NONE;
		for (auto flag : config.flags) {
			flags |= flag;
		}
	}
	auto file = fs.getFile(source, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing model: " + source);
	}
	auto bytes = co_await file->readAllTask(control);
	control->check();
	auto imported = std::make_shared<ImportedModel>();
	auto prepare = [imported, source, flags, bytes = std::move(bytes)] {
		if (!AssimpParser::ImportModel(source, bytes, flags, *imported)) {
			throw std::runtime_error("Cannot import model: " + source);
		}
	};
#ifdef __EMSCRIPTEN__
	// Core uses owner-side resource loading on WebAssembly.
	co_await COROUTINE::NextFrame();
	control->check();
	prepare();
#else
	auto& tasks = ServiceManager::Get<TASK::TaskSystem>();
	// Assimp completes the import before returning. The worker owns all inputs and the CPU result.
	const auto ticket = tasks.dispatch("Import model", std::move(prepare));
	while (!ticket.ready()) {
		control->check();
		co_await COROUTINE::NextFrame();
	}
	control->check();
	const auto result = ticket.get();
	if (result.Exception) {
		std::rethrow_exception(result.Exception);
	}
	if (result.Outcome == TASK::BackgroundOutcome::Cancelled) {
		throw ResourceLoadCancelled();
	}
	if (!ticket.accepted()) {
		throw std::runtime_error("Cannot schedule model import for " + source + ": " + TASK::backgroundRejectionMessage(result.Rejection));
	}
#endif
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	control->check();
	auto model = ServiceManager::Get<RENDER::Renderer>().createModel(path, nullptr, createCacheDeleter(path));
	if (!model) {
		throw std::runtime_error("Cannot create model: " + path);
	}
	AssimpParser::CreateModelResources(std::move(*imported), *model);
	model->computeBoundingSphere();
	model->setPath(path);
	if (candidate.descriptor && !PublishConfig(path, candidate)) {
		throw ResourceLoadCancelled();
	}
	auto published = registerResource(path, std::move(model), control.get());
	addFileWatchSubscribe(path, { path, source }, published);
	co_return published;
}

namespace {
	ResourcePtr<RENDER::ModelInterface> prepareModelReload(const RENDER::ModelResource& config) {
		auto usedFlags = config.path == config.pathModel ? ModelLoader::getDefaultFlag() : ModelParserFlags::NONE;
		for (auto flag : config.flags) {
			usedFlags |= flag;
		}
		return ModelLoader::Create(config.pathModel, usedFlags);
	}
}

void ModelLoader::Reload(RENDER::ModelInterface& model, const RENDER::ModelResource& config) {
	if (auto replacement = prepareModelReload(config)) {
		model.swapImportedState(*replacement);
	}
}

// TODO: remove
ResourcePtr<IKIGAI::RENDER::ModelInterface> ModelLoader::CreateFromFile(
	const std::string& path, ModelParserFlags parserFlags, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
	std::string realPath = path.empty() || path.front() != '/' ? "/" + path : path;
	auto model = Create(realPath, parserFlags == ModelParserFlags::NONE ? getDefaultFlag() : parserFlags, allocator, deleter);
	if (model) {
		model->setPath(path);
	}
	return model;
}

ResourcePtr<IKIGAI::RENDER::ModelInterface> ModelLoader::CreateFromResource(
	const std::string& path, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
	auto config = LoadConfig(path);
	return CreateFromResource(config, allocator, deleter);
}

ResourcePtr<IKIGAI::RENDER::ModelInterface> ModelLoader::CreateFromResource(
	const RENDER::ModelResource& config, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
	ModelParserFlags parserFlags = ModelParserFlags::NONE;
	for (auto e : config.flags) {
		parserFlags |= e;
	}
	// Грузим сам файл модели
	auto model = CreateFromFile(config.pathModel, parserFlags, allocator, deleter);
	return model;
}

ResourcePtr<RENDER::ModelInterface> ModelLoader::Create(
	const std::string& filepath, ModelParserFlags parserFlags, UTILS::IAllocator* allocator, RENDER::ModelDeleter deleter) {
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	auto result = render.createModel(filepath, allocator, deleter);
	if (!result) {
		ASSERT("ModelLoader::Create: Failed to allocate model object");
		return nullptr;
	}

	auto& fs = ServiceManager::Get<FileSystem>();
	auto file = fs.getFile(filepath);
	auto data = file && file->isValid() ? file->read() : std::vector<uint8_t> {};
	if (data.empty()) {
		IKIGAI_COUT("Model file is missing or empty: '" << filepath << "'; opened: " << (file && file->isValid())
														<< "; native path: " << fs.getAbsolutePath(filepath).value_or("<not found>")
														<< "; content root: " << Config::ROOT);
		return nullptr;
	}

	AssimpParser parser;
	if (parser.LoadModel(filepath, data, result, parserFlags)) {
		result->computeBoundingSphere();
		return result;
	}
	return nullptr;
}

ResourcePtr<RENDER::ModelInterface> ModelLoader::CreateVerts(const std::string& filepath, ModelParserFlags parserFlags,
	std::vector<std::vector<Vertex>>& _globalVerticesPerMesh, std::vector<std::vector<uint32_t>>& _globalIndicesPerMesh) {

	auto& render = ServiceManager::Get<RENDER::Renderer>();
	auto result = render.createModel(filepath);
	if (!result) {
		ASSERT("ModelLoader::CreateVerts: Failed to allocate model object");
		return nullptr;
	}
	auto data = ServiceManager::Get<FileSystem>().getFile(filepath)->read();
	if (data.empty()) {
		return nullptr;
	}

	AssimpParser parser;
	if (parser.LoadVertexes(filepath, data, result, parserFlags, _globalVerticesPerMesh, _globalIndicesPerMesh)) {
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
	// flags |= ModelParserFlags::JOIN_IDENTICAL_VERTICES;
	// flags |= ModelParserFlags::IMPROVE_CACHE_LOCALITY;
	// flags |= ModelParserFlags::FIND_INVALID_DATA;
	// flags |= ModelParserFlags::FIND_INSTANCES;
	// flags |= ModelParserFlags::OPTIMIZE_MESHES;
	// flags |= ModelParserFlags::OPTIMIZE_GRAPH;
	// flags |= ModelParserFlags::DEBONE;
	//  flags |= IKIGAI::RESOURCES::ModelParserFlags::PRE_TRANSFORM_VERTICES;
	//  flags |= IKIGAI::RESOURCES::ModelParserFlags::FLIP_WINDING_ORDER;
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
	std::unordered_set<std::string> paths = { path };
	if (type == ELoadingType::FILE) {
		res = CreateFromFile(path, ModelParserFlags::NONE, nullptr, createCacheDeleter(path));
	}
	else if (type == ELoadingType::RESOURCE) {
		const auto candidate = ReadConfigCandidate(path);
		const auto& config = *candidate.descriptor;
		paths.insert(config.pathModel);
		res = CreateFromResource(config, nullptr, createCacheDeleter(path));
		if (res && !PublishConfig(path, candidate)) {
			return nullptr;
		}
	}
	else {
		// TODO: MEMORY/DESCRIPTOR if needed
		res = CreateFromFile(path, ModelParserFlags::NONE, nullptr, createCacheDeleter(path));
	}

	{ addFileWatchSubscribe(path, paths, res); }

	return res;
}

bool ModelLoader::reloadResource(std::weak_ptr<RENDER::ModelInterface> weakRes, const std::string& path) {
	if (auto res = weakRes.lock()) {
		std::unordered_set<std::string> paths = { path };
		RENDER::ModelResource config;
		ConfigCandidate candidate;
		if (path.ends_with(".model")) {
			candidate = ReadConfigCandidate(path, true);
			config = *candidate.descriptor;
			paths.insert(config.pathModel);
		}
		else {
			config.path = path;
			config.pathModel = path;
		}

		auto replacement = prepareModelReload(config);
		if (!replacement) {
			return false;
		}
		const auto swap = [&]() noexcept { res->swapImportedState(*replacement); };
		if (candidate.descriptor) {
			if (!CommitReload(path, candidate, swap)) {
				return false;
			}
		}
		else {
			swap();
		}
		addFileWatchSubscribe(path, paths, weakRes);
		return true;
	}
	return false;
}
}
