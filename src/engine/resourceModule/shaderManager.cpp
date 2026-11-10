#include "shaderManager.h"

#include "coreModule/ecs/components/transform.h"
#include "fileWatcher.h"
#include "renderModule/backends/gl/shaderGl.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderModule/backends/interface/shaderInterface.h"
#include "renderModule/render.h"
#include "serviceManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <regex>
#include <resourceModule/fileSystem/fileSystem.h>
#include <set>
#include <sstream>

namespace {
std::string readShaderSource(const std::string& input, std::set<std::string>& stack, std::unordered_set<std::string>* dependencies = nullptr) {
	const auto path = std::filesystem::path(input).lexically_normal().generic_string();
	if (dependencies) {
		dependencies->insert(path);
	}
	if (!stack.insert(path).second) {
		throw std::runtime_error("Shader include cycle: " + path);
	}
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();
	auto file = fs.getFile(path);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing shader source: " + path);
	}
	std::string source = file->readStr();
	uint32_t magic = 0;
	if (source.size() >= sizeof(magic)) {
		std::memcpy(&magic, source.data(), sizeof(magic));
	}
	if (magic == 0x07230203) {
		if (stack.size() != 1) {
			throw std::runtime_error("A shader include must contain source text: " + path);
		}
		stack.erase(path);
		return source;
	}
	static const std::regex include(R"inc(^[ \t]*#[ \t]*include[ \t]+"([^"]+)"[^\n]*)inc");
	std::string result, line;
	std::istringstream stream(source);
	while (std::getline(stream, line)) {
		std::smatch match;
		if (std::regex_match(line, match, include)) {
			const auto child = (std::filesystem::path(path).parent_path() / match[1].str()).lexically_normal().generic_string();
			result += readShaderSource(child, stack, dependencies);
		}
		else {
			result += line;
		}
		result += '\n';
	}
	stack.erase(path);
	return result;
}

IKIGAI::RESOURCES::ResourcePtr<IKIGAI::RENDER::ShaderInterface> GetDefaultPinkShader() {
	static IKIGAI::RESOURCES::ResourcePtr<IKIGAI::RENDER::ShaderInterface> defaultShader;
	if (defaultShader) {
		return defaultShader;
	}

	const std::string defaultVertex = R"(
#version 450 core
layout (location = 0) in vec3 aPos;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 model;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

	const std::string defaultFragment = R"(
#version 450 core
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.0, 1.0, 1.0);
}
)";

	IKIGAI::RENDER::ShaderResource res;
	res.path = "PINK_SHADER";
	res.sources[IKIGAI::RENDER::ShaderType::VERTEX] = defaultVertex;
	res.sources[IKIGAI::RENDER::ShaderType::FRAGMENT] = defaultFragment;

	auto& render = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RENDER::Renderer>();
	defaultShader = render.createShader(res);
	return defaultShader;
}
}

namespace IKIGAI::RESOURCES {
std::unordered_set<std::string> ShaderLoader::ResolveSources(RENDER::ShaderResource& resource) {
	std::unordered_set<std::string> dependencies;
	if (!resource.path.empty()) {
		dependencies.insert(resource.path);
	}
	if (resource.supportsPbrLayers && (resource.compiledPbrLayerCount > 3 || resource.useBinary)) {
		throw std::invalid_argument("Layered PBR requires source shaders and at most three additional layers");
	}
	for (const auto& [stage, path] : resource.getPaths()) {
		dependencies.insert(std::filesystem::path(path).lexically_normal().generic_string());
		if (resource.sources.contains(stage)) {
			continue;
		}
		std::set<std::string> stack;
		auto source = readShaderSource(path, stack, &dependencies);
		if (resource.supportsPbrLayers) {
			const auto version = source.find("#version");
			const auto end = source.find('\n', version);
			if (version == std::string::npos || end == std::string::npos) {
				throw std::invalid_argument("Missing GLSL version in " + path);
			}
			source.insert(end + 1, "#define IKIGAI_PBR_LAYER_COUNT " + std::to_string(resource.compiledPbrLayerCount) + "\n");
		}
		resource.sources.emplace(stage, std::move(source));
	}
	return dependencies;
}

void ShaderLoader::addFileWatchSubscribe(
	const std::string& path, const std::unordered_set<std::string>& previous, std::weak_ptr<RENDER::ShaderInterface> shader) {
	const auto current = shader.lock();
	watchResourceFiles(path, current ? current->mSourceDependencies : previous, shader, current ? current->mPath : path);
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::loadMaterialShader(const std::string& path, unsigned additionalLayers) {
	const auto normalizedPath = NormalizePath(path);
	const auto key = normalizedPath + "@pbr-layers-" + std::to_string(additionalLayers);
	if (auto shader = getResource(key)) {
		return shader;
	}
	const auto candidate = ReadConfigCandidate(normalizedPath);
	auto resource = *candidate.descriptor;
	if (!resource.supportsPbrLayers) {
		if (additionalLayers != 0) {
			throw std::invalid_argument("Shader does not support PbrLayers: " + normalizedPath);
		}
		return loadResource(normalizedPath);
	}
	resource.path = normalizedPath;
	resource.compiledPbrLayerCount = additionalLayers;
	auto shader = CreateFromResource(resource, nullptr, createCacheDeleter(key));
	if (!shader || !PublishConfig(normalizedPath, candidate)) {
		return nullptr;
	}
	addFileWatchSubscribe(key, shader->mSourceDependencies, shader);
	return registerResource(key, shader);
}

COROUTINE::Task<ResourcePtr<RENDER::ShaderInterface>> ShaderLoader::createResourceTask(
	std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	auto candidate = BeginConfigLoad(path);
	if (!candidate.descriptor) {
		auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Missing shader: " + path);
		}
		auto bytes = co_await file->readAllTask(control);
		ParseConfigCandidate(candidate, std::string(bytes.begin(), bytes.end()), path);
	}
	co_await COROUTINE::NextFrame();
	control->check();
	co_return loadResourceChecked(path, type, std::move(candidate), control.get());
}
RENDER::RayTracingProgramResource ShaderLoader::LoadProgramResource(const std::string& path) {
	auto resource = LoadConfig(path).getProgramResource();
	for (auto& shader : resource.Shaders) {
		if (RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::DIRECTX12 && shader.BackendPaths.contains("DIRECTX12")) {
			shader.Path = shader.BackendPaths.at("DIRECTX12");
			shader.Language = "HLSL";
			shader.Source.clear();
		}
		if (!shader.Source.empty()) {
			continue;
		}
		std::set<std::string> stack;
		shader.Source = readShaderSource(shader.Path, stack);
	}
	return resource;
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::createResource(const std::string& path) {
	return createResource(path, ELoadingType::RESOURCE);
}

std::shared_ptr<RENDER::ShaderInterface> ShaderLoader::createResource(const std::string& path, ELoadingType type) {
	return createResource(path, type, std::any());
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
	if (type != ELoadingType::RESOURCE) {
		return nullptr;
	}
	const auto* prepared = std::any_cast<ConfigCandidate>(&data);
	const auto candidate = prepared && IsCurrentConfig(path, *prepared) ? *prepared : ReadConfigCandidate(path);
	auto res = CreateFromResource(*candidate.descriptor, nullptr, createCacheDeleter(path));
	if (res && PublishConfig(path, candidate)) {
		addFileWatchSubscribe(path, res->mSourceDependencies, res);
		return res;
	}
	return nullptr;
}

bool ShaderLoader::reloadResource(std::weak_ptr<RENDER::ShaderInterface> weakRes, const std::string& key) {
	if (auto shader = weakRes.lock()) {
		const auto sourcePath = shader->mPath;
		const auto candidate = ReadConfigCandidate(sourcePath, true);
		auto config = *candidate.descriptor;
		config.compiledPbrLayerCount = shader->compiledPbrLayerCount();
		if (shader->supportsPbrLayers() && !config.supportsPbrLayers) {
			throw std::runtime_error("Cannot remove layered PBR contract during shader reload: " + sourcePath);
		}
		auto replacement = CreateFromResource(config);
		shader->prepareReloadedShader(replacement);
		if (!commitResourceReload(key, shader, sourcePath, candidate, [&]() noexcept {
				if (!shader->adoptReloadedShader(*replacement)) {
					return false;
				}
				shader->mSourceDependencies.swap(replacement->mSourceDependencies);
				return true;
			})) {
			return false;
		}
		addFileWatchSubscribe(key, shader->mSourceDependencies, shader);
		return true;
	}
	return false;
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::Create(const std::string& _filePath, UTILS::IAllocator* allocator, RENDER::ShaderDeleter deleter) {
	RENDER::ShaderResource config = LoadConfig(_filePath);
	return CreateFromResource(config, allocator, deleter);
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::CreateFromResource(
	const RENDER::ShaderResource& res, UTILS::IAllocator* allocator, RENDER::ShaderDeleter deleter) try {
	if (!res.Shaders.empty() || !res.Groups.empty()) {
		throw std::invalid_argument("Named shader modules/groups require a Program pipeline step: " + res.path);
	}
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	auto resCopy = res;
	auto dependencies = ResolveSources(resCopy);

	auto shader = render.createShader(resCopy, allocator, deleter);
	if (!shader) {
		throw std::runtime_error("Failed to compile shader resource: " + res.path);
	}
	shader->mSourceDependencies = std::move(dependencies);
	return shader;
} catch (const RENDER::SpirvError& error) {
	if (res.path.empty()) {
		throw;
	}
	throw RENDER::SpirvError("Shader asset " + res.path + ": " + error.what());
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::CreateFromSource(const std::string& vertexShader, const std::string& fragmentShader,
	const std::string& geometryShader, const std::string& tessCompShader, const std::string& tessEvoluationShader, const std::string& computeShader,
	UTILS::IAllocator* allocator, RENDER::ShaderDeleter deleter) {

	RENDER::ShaderResource res;
	res.path = "MEMORY";
	if (!vertexShader.empty()) {
		res.sources[RENDER::ShaderType::VERTEX] = vertexShader;
	}
	if (!fragmentShader.empty()) {
		res.sources[RENDER::ShaderType::FRAGMENT] = fragmentShader;
	}
	if (!geometryShader.empty()) {
		res.sources[RENDER::ShaderType::GEOMETRY] = geometryShader;
	}
	if (!tessCompShader.empty()) {
		res.sources[RENDER::ShaderType::TESSELLATION_CONTROL] = tessCompShader;
	}
	if (!tessEvoluationShader.empty()) {
		res.sources[RENDER::ShaderType::TESSELLATION_EVALUATION] = tessEvoluationShader;
	}
	if (!computeShader.empty()) {
		res.sources[RENDER::ShaderType::COMPUTE] = computeShader;
	}

	auto& render = ServiceManager::Get<RENDER::Renderer>();
	auto shader = render.createShader(res, allocator, deleter);
	if (!shader) {
		ASSERT("Failed to compile shader from source");
		return GetDefaultPinkShader();
	}
	return shader;
}

void ShaderLoader::Recompile(RENDER::ShaderInterface& shader, const RENDER::ShaderResource& res) {
	auto prepared = res;
	auto dependencies = ResolveSources(prepared);
	shader.recompile(prepared);
	shader.mSourceDependencies = std::move(dependencies);
}
}
