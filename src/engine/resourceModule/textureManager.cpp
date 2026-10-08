#include "textureManager.h"
#include "serviceManager.h"
#include <resourceModule/fileSystem/fileSystem.h>
#include <filesystem>

#include "fileWatcher.h"
#include "renderModule/render.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "utilsModule/jsonLoader.h"

namespace {
	constexpr auto TEXTURE_RES_EXT = ".texture";
	constexpr auto TEXTURE_HDR_EXT = ".hdr";
}

namespace IKIGAI::RESOURCES {

TextureLoader::LoadedTextureData TextureLoader::LoadFileData(const std::string& realPath, bool generateMipmap, bool isHDR) {
	LoadedTextureData result;
	result.descriptor.path      = realPath;
	result.descriptor.useMipmap = generateMipmap;
	result.descriptor.isFloat   = isHDR;

	auto file = ServiceManager::Get<FileSystem>().getFile(realPath, FileMode::READ);
	if (!file || !file->isValid()) {
		ASSERT("TextureLoader::LoadFileData — cannot open file");
		return result;
	}
	result.fileData = file->read();
	file->close();
	return result;
}

TextureLoader::TextureLoader() = default;

TextureLoader::~TextureLoader() = default;

void TextureLoader::InitDefaultTexture() {
	if (sDefaultTexture) {
		return;
	}
	sDefaultTexture = CreateColor(255, 20, 147, 255, false);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::GetDefaultTexture() {
	if (!sDefaultTexture) {
		InitDefaultTexture();
	}
	return sDefaultTexture;
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromFile(const std::string& filepath, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto loaded = LoadFileData(filepath, generateMipmap, false);
	if (!loaded.valid()) {
		ASSERT("TextureLoader::CreateFromFile — failed to read file");
		return GetDefaultTexture();
	}

	auto& render = ServiceManager::Get<RENDER::Renderer>();
	return render.createTexture(filepath, loaded.fileData, generateMipmap, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromResource(const std::string& resPath, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto config = LoadConfig(resPath);
	return CreateFromResource(config, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromResource(const RENDER::TextureResource& res, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	if (!res.pathTexture.empty() || !res.colorData.empty()) {
		auto& render = ServiceManager::Get<RENDER::Renderer>();
		std::vector<std::vector<uint8_t>> fileData;
		for (const auto& p : res.pathTexture) {
			auto file = ServiceManager::Get<FileSystem>().getFile(p, FileMode::READ);
			if (file && file->isValid()) {
				fileData.push_back(file->read());
			}
			else {
				ASSERT("TextureLoader::CreateFromResource — failed to read file");
				return GetDefaultTexture();
			}
		}
		return render.createTexture(res, fileData, allocator, deleter);
	}
	ASSERT("TextureLoader::CreateFromResource — failed to read file");
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateAtlasFromFile(const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto loaded = LoadFileData(path, generateMipmap, false);
	if (!loaded.valid()) {
		ASSERT("TextureLoader::CreateAtlasFromFile — failed to read file");
		return GetDefaultTexture();
	}
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	RENDER::TextureResource res;
	res.path = path;
	res.pathTexture = { path };
	res.useMipmap = generateMipmap;
	return render.createTextureAtlas(res, {loaded.fileData}, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromFileHDR(const std::string& filepath, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto loaded = LoadFileData(filepath, generateMipmap, true);
	if (!loaded.valid()) {
		ASSERT("TextureLoader::CreateFromFileHDR — failed to read file");
		return GetDefaultTexture();
	}
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	RENDER::TextureResource res;
	res.path = filepath;
	res.pathTexture = { filepath };
	res.useMipmap = generateMipmap;
	res.isFloat = true;
	return render.createTexture(res, {loaded.fileData}, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	RENDER::TextureResource res;
	res.colorData  = { r, g, b, a };
	res.width = res.height = 1;
	res.channels   = 4;
	res.pixelType  = RENDER::PixelFormat::RGBA_INT;
	res.useMipmap  = generateMipmap;
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	return render.createTexture(res, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateColor(uint32_t data, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	return CreateColor(
		(data >> 24) & 0xFF,
		(data >> 16) & 0xFF,
		(data >>  8) & 0xFF,
		 data        & 0xFF,
		generateMipmap, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromMemory(const std::string& path, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	return render.createTexture(path, data, generateMipmap, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createResource(const std::string& path) {
	auto& fs = ServiceManager::Get<FileSystem>();
	auto ext = fs.getFileExtension(path);
	if (ext == TEXTURE_RES_EXT) {
		return createFromResource(path);
	}
	if (ext == TEXTURE_HDR_EXT) {
		return createFromFileHDR(path, true);
	}
	return createFromFile(path, true);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createResource(const std::string& path, ELoadingType type) {
	return createResource(path, type, std::any{});
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
	if (type == ELoadingType::RESOURCE) {
		if (data.has_value()) {
			if (auto* res = std::any_cast<RENDER::TextureResource>(&data)) {
				if (auto newResource = CreateFromResource(*res))
					return registerResource(path, newResource);
				return GetDefaultTexture();
			}
		}
		return createFromResource(path);
	}
	if (type == ELoadingType::FILE) {
		auto& fs = ServiceManager::Get<FileSystem>();
		auto ext = fs.getFileExtension(path);
		if (ext == TEXTURE_HDR_EXT) {
			return createFromFileHDR(path, true);
		}
		return createFromFile(path, true);
	}
	if (type == ELoadingType::MEMORY) {
		if (data.has_value()) {
			if (auto* mem = std::any_cast<std::vector<uint8_t>>(&data)) {
				return createFromMemory(path, *mem, true);
			}
		}
	}
	ASSERT("TextureLoader::createResource — unexpected ELoadingType type");
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createFromResource(const std::string& path) {
	if (auto resource = getResource(path)) {
		return resource;
	}

	RENDER::TextureResource cachedDescriptor;
	if (auto it = sResourceCache.find(path); it != sResourceCache.end()) {
		cachedDescriptor = it->second;
	}
	else {
		auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
		if (!file || !file->isValid()) {
			ASSERT("TextureLoader: cannot open .res file");
			return GetDefaultTexture();
		}
		auto parseResult = UTILS::FromJsonStr<RENDER::TextureResource>(file->readStr());
		if (parseResult.isErr()) {
			ASSERT("TextureLoader: failed to parse .res file");
			return GetDefaultTexture();
		}
		cachedDescriptor = parseResult.unwrap();
		cachedDescriptor.path = path;
		sResourceCache[path] = cachedDescriptor;
	}

	if (auto newResource = CreateFromResource(cachedDescriptor, nullptr, createCacheDeleter(path))) {
		auto& fs = ServiceManager::Get<FileSystem>();
		auto basePathOpt = fs.getFilePath(path);
		if (!basePathOpt) {
			ASSERT("Path not found");
		}

		{// FileWatch
			std::unordered_set<std::string> paths;
			paths.insert(*basePathOpt);
			for (const auto& imgPath : cachedDescriptor.pathTexture) {
				auto pathOpt = fs.getFilePath(imgPath);
				if (pathOpt) {
					paths.insert(*pathOpt);
				}
			}
			addFileWatchSubscribe(*basePathOpt, paths, newResource);
		}

		return registerResource(path, newResource);
	}
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createFromFile(const std::string& path, bool generateMipmap) {
	if (auto resource = getResource(path)) {
		return resource;
	}
	if (auto newResource = CreateFromFile(path, generateMipmap, nullptr, createCacheDeleter(path))) {

		{// FileWatch
			auto& fs = ServiceManager::Get<FileSystem>();
			addFileWatchSubscribe(path, {path}, newResource);
		}
		return registerResource(path, newResource);
	}
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createFromFileHDR(const std::string& path, bool generateMipmap) {
	if (auto resource = getResource(path)) {
		return resource;
	}
	if (auto newResource = CreateFromFileHDR(path, generateMipmap, nullptr, createCacheDeleter(path))) {
		{// FileWatch
			auto& fs = ServiceManager::Get<FileSystem>();
			if (auto basePathOpt = fs.getFilePath(path)) {
				addFileWatchSubscribe(*basePathOpt, {*basePathOpt}, newResource);
			}
		}
		return registerResource(path, newResource);
	}
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createColor(const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool generateMipmap) {
	if (auto resource = getResource(name)) {
		return resource;
	}
	if (auto newResource = CreateColor(r, g, b, a, generateMipmap, nullptr, createCacheDeleter(name))) {
		return registerResource(name, newResource);
	}
	ASSERT("TextureLoader::createColor — can not create texture");
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createColor(const std::string& name, uint32_t data, bool generateMipmap) {
	if (auto resource = getResource(name)) {
		return resource;
	}
	if (auto newResource = CreateColor(data, generateMipmap, nullptr, createCacheDeleter(name))) {
		return registerResource(name, newResource);
	}
	ASSERT("TextureLoader::createColor — can not create texture");
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createFromMemory(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap) {
	if (auto resource = getResource(name)) {
		return resource;
	}
	if (auto newResource = CreateFromMemory(name, data, generateMipmap, nullptr, createCacheDeleter(name))) {
		return registerResource(name, newResource);
	}
	ASSERT("TextureLoader::createFromMemory — can not create texture");
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createAtlasFromFile(const std::string& path, bool generateMipmap) {
	if (auto resource = getResource(path)) {
		return resource;
	}
	if (auto newResource = CreateAtlasFromFile(path, generateMipmap, nullptr, createCacheDeleter(path))) {
		return registerResource(path, newResource);
	}
	ASSERT("TextureLoader::createAtlasFromFile — can not create texture");
	return GetDefaultTexture();
}

bool TextureLoader::reloadResource(std::weak_ptr<RENDER::TextureInterface> weakRes, const std::string& path) {
	if (auto res = weakRes.lock()) {
		RENDER::TextureResource config;
		if (path.ends_with(".texture")) {
			config = LoadConfig(path);
			AddConfigToCache(path, config);
		} else if (path.ends_with(".hdr")) {
			auto generateMipmap = weakRes.lock()->getUseMipMap();
			config.path = path;
			config.pathTexture = {path};
			config.useMipmap = generateMipmap;
			config.isFloat = true;
		} else {
			auto generateMipmap = weakRes.lock()->getUseMipMap();
			config.path = path;
			config.pathTexture = {path};
			config.useMipmap = generateMipmap;
		}
		if (!config.pathTexture.empty() || !config.colorData.empty()) {
			std::vector<std::vector<uint8_t>> fileData;
			for (const auto& p : config.pathTexture) {
				auto file = ServiceManager::Get<FileSystem>().getFile(p, FileMode::READ);
				if (file && file->isValid()) {
					fileData.push_back(file->read());
				}
				file->close();
			}
			res->recreate(config, fileData);
		}
		return true;
	}
	return false;
}

} // namespace IKIGAI::RESOURCES
