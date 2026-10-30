#include "shaderManager.h"

#include "coreModule/ecs/components/transform.h"
#include "fileWatcher.h"
#include "renderModule/backends/gl/shaderGl.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderModule/render.h"
#include "serviceManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include <filesystem>
#include <functional>
#include <regex>
#include <resourceModule/fileSystem/fileSystem.h>
#include <set>
#include <sstream>

namespace {
std::string readProgramSource(const std::string& path, std::set<std::string>& stack, std::unordered_set<std::string>* dependencies = nullptr) {
	if (dependencies) {
		dependencies->insert(path);
	}
	if (!stack.insert(path).second) {
		throw std::runtime_error("RT shader include cycle: " + path);
	}
	auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>();
	if (!fs.isFileExist(path)) {
		throw std::runtime_error("Missing RT shader: " + path);
	}
	std::string source = fs.getFile(path)->readStr();
	const std::regex include(R"inc(^[ \t]*#include[ \t]+"([^"]+)"[^\n]*)inc");
	std::string result, line;
	std::istringstream stream(source);
	while (std::getline(stream, line)) {
		std::smatch match;
		if (std::regex_match(line, match, include)) {
			const auto child = (std::filesystem::path(path).parent_path() / match[1].str()).lexically_normal().generic_string();
			result += readProgramSource(child, stack, dependencies);
		}
		else {
			result += line;
		}
		result += '\n';
	}
	stack.erase(path);
	return result;
}

void preparePbrSources(IKIGAI::RENDER::ShaderResource& resource, std::unordered_set<std::string>* dependencies = nullptr) {
	if (!resource.supportsPbrLayers) {
		return;
	}
	if (resource.compiledPbrLayerCount > 3 || resource.useBinary) {
		throw std::invalid_argument("Layered PBR requires source shaders and at most three additional layers");
	}
	for (const auto& [stage, path] : resource.getPaths()) {
		std::set<std::string> stack;
		auto source = readProgramSource(path, stack, dependencies);
		const auto version = source.find("#version");
		const auto end = source.find('\n', version);
		if (version == std::string::npos || end == std::string::npos) {
			throw std::invalid_argument("Missing GLSL version in " + path);
		}
		source.insert(end + 1, "#define IKIGAI_PBR_LAYER_COUNT " + std::to_string(resource.compiledPbrLayerCount) + "\n");
		resource.sources[stage] = std::move(source);
	}
}
std::string readFileWithInclude(const std::string& path) {
	auto dir = std::filesystem::path { path }.parent_path();

	if (!IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>().isFileExist(path)) {
		return "";
	}

	auto content = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>().getFile(path)->readStr();

	size_t start = 0;
	size_t pos;
	while ((pos = content.find("#include", start)) != std::string::npos) {
		if (pos > 0 && content[pos - 1] == '/') {
			start = pos + 1;
			continue;
		}
		auto comma1 = content.find("\"", pos);
		if (comma1 == std::string::npos) {
			break; // or throw
		}
		auto comma2 = content.find("\"", comma1 + 1);
		if (comma2 == std::string::npos) {
			break; // or throw
		}

		auto includePath = std::string(content.begin() + comma1 + 1, content.begin() + comma2);
		std::string fullIncludePath = (dir / includePath).lexically_normal().generic_string();
		auto newContent = readFileWithInclude(fullIncludePath);
		content.erase(pos, comma2 - pos + 1);
		content.insert(pos, newContent);
		start = pos + newContent.length();
	}
	return content;
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
ResourcePtr<RENDER::ShaderInterface> ShaderLoader::loadMaterialShader(const std::string& path, unsigned additionalLayers) {
	const auto normalizedPath = NormalizePath(path);
	const auto key = normalizedPath + "@pbr-layers-" + std::to_string(additionalLayers);
	if (auto shader = getResource(key)) {
		return shader;
	}
	auto resource = HasConfig(normalizedPath) ? *GetConfig(normalizedPath) : LoadConfig(normalizedPath);
	if (!resource.supportsPbrLayers) {
		if (additionalLayers != 0) {
			throw std::invalid_argument("Shader does not support PbrLayers: " + normalizedPath);
		}
		return loadResource(normalizedPath);
	}
	resource.path = normalizedPath;
	resource.compiledPbrLayerCount = additionalLayers;
	std::unordered_set<std::string> dependencies { normalizedPath };
	preparePbrSources(resource, &dependencies);
	auto shader = CreateFromResource(resource, nullptr, createCacheDeleter(key));
	addFileWatchSubscribe(key, dependencies, shader);
	return registerResource(key, shader);
}

COROUTINE::Task<ResourcePtr<RENDER::ShaderInterface>> ShaderLoader::createResourceTask(
	std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing shader: " + path);
	}
	auto bytes = co_await file->readAllTask(control);
	auto parsed = UTILS::FromJsonStr<RENDER::ShaderResource>(std::string(bytes.begin(), bytes.end()));
	if (parsed.isErr()) {
		throw std::runtime_error("Invalid shader: " + path);
	}
	auto descriptor = parsed.unwrap();
	descriptor.path = path;
	AddConfigToCache(path, descriptor);
	co_await COROUTINE::NextFrame();
	control->check();
	co_return loadResource(path, type);
}
RENDER::RayTracingProgramResource ShaderLoader::LoadProgramResource(const std::string& path) {
	auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	if (!fs.isFileExist(path)) {
		throw std::runtime_error("Missing shader program: " + path);
	}
	auto parsed = UTILS::FromJsonStr<RENDER::ShaderResource>(fs.getFile(path)->readStr());
	if (parsed.isErr()) {
		throw std::runtime_error("Invalid shader program " + path + ": " + parsed.unwrapErr().text);
	}
	auto resource = parsed.unwrap().getProgramResource();
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
		shader.Source = readProgramSource(shader.Path, stack);
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
	RENDER::ShaderResource config;
	if (HasConfig(path)) {
		config = *GetConfig(path);
	}
	else {
		config = LoadConfig(path);
		AddConfigToCache(path, config);
	}

	ResourcePtr<RENDER::ShaderInterface> res;
	if (type == ELoadingType::RESOURCE) {
		res = CreateFromResource(config, nullptr, createCacheDeleter(path));
	}
	else if (type == ELoadingType::FILE || type == ELoadingType::MEMORY) {
		// NOT SUPPORT
	}
	{
		std::unordered_set<std::string> paths = { path };
		for (auto& [type, shaderPath] : config.paths) {
			paths.insert(shaderPath);
		}
		addFileWatchSubscribe(path, paths, res);
	}

	return res;
}

bool ShaderLoader::reloadResource(std::weak_ptr<RENDER::ShaderInterface> weakRes, const std::string& path) {
	if (auto shader = weakRes.lock()) {
		const auto sourcePath = shader->mPath;
		std::unordered_set<std::string> paths = { sourcePath };
		auto config = LoadConfig(sourcePath);
		AddConfigToCache(sourcePath, config);
		config.compiledPbrLayerCount = shader->compiledPbrLayerCount();
		if (shader->supportsPbrLayers() && !config.supportsPbrLayers) {
			throw std::runtime_error("Cannot remove layered PBR contract during shader reload: " + sourcePath);
		}
		preparePbrSources(config, &paths);

		for (auto type : { RENDER::ShaderType::VERTEX, RENDER::ShaderType::FRAGMENT, RENDER::ShaderType::GEOMETRY,
			 RENDER::ShaderType::TESSELLATION_CONTROL, RENDER::ShaderType::TESSELLATION_EVALUATION, RENDER::ShaderType::COMPUTE }) {
			if (config.hasShader(type)) {
				auto p = config.getShaderPath(type);
				if (!config.supportsPbrLayers) {
					config.sources[type] = readFileWithInclude(p);
				}
				paths.insert(p);
			}
		}
		shader->recompile(config);
		addFileWatchSubscribe(path, paths, weakRes);
		return true;
	}
	return false;
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::Create(const std::string& _filePath, UTILS::IAllocator* allocator, RENDER::ShaderDeleter deleter) {
	RENDER::ShaderResource config = LoadConfig(_filePath);
	return CreateFromResource(config, allocator, deleter);
}

ResourcePtr<RENDER::ShaderInterface> ShaderLoader::CreateFromResource(
	const RENDER::ShaderResource& res, UTILS::IAllocator* allocator, RENDER::ShaderDeleter deleter) {
	if (!res.Shaders.empty() || !res.Groups.empty()) {
		throw std::invalid_argument("Named shader modules/groups require a Program pipeline step: " + res.path);
	}
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	const std::string path = res.path;

	auto resCopy = res;
	if (resCopy.supportsPbrLayers && resCopy.sources.empty()) {
		preparePbrSources(resCopy);
	}
	if (resCopy.sources.empty()) {
		for (auto type : { RENDER::ShaderType::VERTEX, RENDER::ShaderType::FRAGMENT, RENDER::ShaderType::GEOMETRY,
			 RENDER::ShaderType::TESSELLATION_CONTROL, RENDER::ShaderType::TESSELLATION_EVALUATION, RENDER::ShaderType::COMPUTE }) {
			if (resCopy.hasShader(type)) {
				resCopy.sources[type] = readFileWithInclude(resCopy.getShaderPath(type));
			}
		}
	}

	auto shader = render.createShader(resCopy, allocator, deleter);
	if (!shader) {
		if (res.supportsPbrLayers) {
			throw std::runtime_error("Failed to compile layered PBR shader: " + res.path);
		}
		ASSERT("Failed to compile shader from resource");
		return GetDefaultPinkShader();
	}
	return shader;
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
	shader.recompile(res);
}
}
