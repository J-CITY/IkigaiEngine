#include "textureManager.h"
#include "serviceManager.h"
#include <filesystem>
#include <resourceModule/fileSystem/fileSystem.h>

#include "fileWatcher.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "renderModule/render.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/stdLoader.h"
#include <bit>
#include <limits>

namespace {
constexpr auto TEXTURE_RES_EXT = ".texture";
constexpr auto TEXTURE_HDR_EXT = ".hdr";
}

namespace IKIGAI::RESOURCES {
namespace {
	std::vector<std::vector<uint8_t>> readTextureSources(const RENDER::TextureResource& descriptor) {
		if (descriptor.pathTexture.empty() && descriptor.colorData.empty() && (descriptor.width <= 0 || descriptor.height <= 0)) {
			throw std::runtime_error("Missing texture data: " + descriptor.path);
		}
		std::vector<std::vector<uint8_t>> data;
		auto& files = ServiceManager::Get<FileSystem>();
		for (const auto& source : descriptor.pathTexture) {
			auto file = files.getFile(source, FileMode::READ);
			auto bytes = file && file->isValid() ? file->read() : std::vector<uint8_t> {};
			if (bytes.empty()) {
				throw std::runtime_error("Missing or empty texture source: " + source);
			}
			data.push_back(std::move(bytes));
		}
		return data;
	}

	size_t texturePixelSize(RENDER::PixelFormat format) {
		using enum RENDER::PixelFormat;
		switch (format) {
		case R_INT:
			return 1;
		case RG_INT:
		case R16_FLOAT:
			return 2;
		case RGB_INT:
			return 3;
		case RGBA_INT:
		case BGRA_INT:
		case R_FLOAT:
		case R32_INT:
		case RG16_FLOAT:
		case R11G11B10_FLOAT:
			return 4;
		case RG_FLOAT:
		case RG32_INT:
		case RGBA16_FLOAT:
			return 8;
		case RGB_FLOAT:
		case RGB32_INT:
			return 12;
		case RGBA_FLOAT:
		case RGBA32_INT:
			return 16;
		default:
			throw std::invalid_argument("Cannot upload color data to a depth texture format");
		}
	}

	void validateTextureReload(RENDER::TextureResource& descriptor, const std::vector<std::vector<uint8_t>>& data) {
		if (!descriptor.colorData.empty()) {
			const auto componentSize = descriptor.isFloat ? sizeof(float) : sizeof(uint8_t);
			const auto bytesPerPixel = std::max(texturePixelSize(descriptor.pixelType), componentSize * std::max(1, descriptor.channels));
			if (descriptor.width <= 0 || descriptor.height <= 0 || descriptor.channels < 1 || descriptor.channels > 4
				|| descriptor.colorData.size() / bytesPerPixel / descriptor.height / std::max(1, descriptor.depth)
					< static_cast<size_t>(descriptor.width)) {
				throw std::runtime_error("Invalid texture color data: " + descriptor.path);
			}
			return;
		}
		if (descriptor.texType == RENDER::TextureType::TEXTURE_CUBE && data.size() != 6) {
			throw std::runtime_error("A cubemap requires six images: " + descriptor.path);
		}
		if (!data.empty()
			&& (texturePixelSize(descriptor.pixelType) > (descriptor.isFloat ? 16u : 4u)
				|| ((descriptor.texType == RENDER::TextureType::TEXTURE_3D || descriptor.texType == RENDER::TextureType::TEXTURE_2D_ARRAY)
					&& (descriptor.depth > 1 || data.size() > 1)))) {
			throw std::runtime_error("Texture source does not match the descriptor's pixel layout: " + descriptor.path);
		}
		int width = 0;
		int height = 0;
		for (const auto& bytes : data) {
			if (bytes.size() > std::numeric_limits<int>::max()) {
				throw std::runtime_error("Texture source is too large: " + descriptor.path);
			}
			int imageWidth = 0;
			int imageHeight = 0;
			int channels = 0;
			const auto length = static_cast<int>(bytes.size());
			auto* pixels = descriptor.isFloat
				? static_cast<void*>(UTILS::STBiLoadfFromMemory(bytes.data(), length, &imageWidth, &imageHeight, &channels, 4))
				: static_cast<void*>(UTILS::STBiLoadFromMemory(bytes.data(), length, &imageWidth, &imageHeight, &channels, 4));
			if (!pixels) {
				throw std::runtime_error("Cannot decode texture source: " + descriptor.path);
			}
			UTILS::STBiImageFree(static_cast<unsigned char*>(pixels));
			if (width && (width != imageWidth || height != imageHeight)) {
				throw std::runtime_error("Texture sources have different dimensions: " + descriptor.path);
			}
			width = imageWidth;
			height = imageHeight;
		}
		if (width > 0) {
			descriptor.width = width;
			descriptor.height = height;
		}
	}
}

TextureLoader::LoadedTextureData TextureLoader::LoadFileData(const std::string& realPath, bool generateMipmap, bool isHDR) {
	LoadedTextureData result;
	result.descriptor.path = realPath;
	result.descriptor.useMipmap = generateMipmap;
	result.descriptor.isFloat = isHDR;

	auto file = ServiceManager::Get<FileSystem>().getFile(realPath, FileMode::READ);
	if (!file || !file->isValid()) {
		return result;
	}
	result.fileData = file->read();
	file->close();
	return result;
}

TextureLoader::TextureLoader() = default;

ResolvedTexture TextureLoader::resolveSprite(const std::string& path) {
	if (!mResolver) {
		mResolver = std::make_unique<ResourceResolver>(ServiceManager::Get<FileSystem>());
	}
	return mResolver->texture(NormalizePath(path));
}
ResourceRequest<RENDER::TextureInterface> TextureLoader::requestSprite(const std::string& path) {
	const auto resolved = resolveSprite(path);
	return resolved.atlas ? requestAtlas(resolved.path)
						  : requestResource(resolved.path, resolved.path.ends_with(".texture") ? ELoadingType::RESOURCE : ELoadingType::FILE);
}
ResourcePtr<RENDER::TextureInterface> TextureLoader::createSprite(const std::string& path) {
	const auto resolved = resolveSprite(path);
	return resolved.atlas ? createAtlasFromFile(resolved.path, false)
						  : loadResource(resolved.path, resolved.path.ends_with(".texture") ? ELoadingType::RESOURCE : ELoadingType::FILE);
}

ResourceRequest<RENDER::TextureInterface> TextureLoader::requestAtlas(std::string path) {
	path = NormalizePath(std::move(path));
	auto& files = ServiceManager::Get<FileSystem>();
	const auto generation = files.generation();
	return mRequests.request(ServiceManager::Get<COROUTINE::Scheduler>(), "atlas:" + std::to_string(generation) + ":" + path,
		[this, path, generation, &files](auto control) {
			control->valid = [&files, generation] { return files.generation() == generation; };
			return createAtlasTask(path, std::move(control));
		});
}
COROUTINE::Task<ResourcePtr<RENDER::TextureInterface>> TextureLoader::createAtlasTask(
	std::string path, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	const auto cacheKey = std::to_string(ServiceManager::Get<FileSystem>().generation()) + ":" + path;
	if (auto ready = mAtlases[cacheKey].lock()) {
		co_return ready;
	}
	auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing texture atlas: " + path);
	}
	auto bytes = co_await file->readAllTask(control);
	control->check();
	RENDER::TextureResource descriptor;
	descriptor.path = path;
	descriptor.pathTexture = { path };
	descriptor.useMipmap = false;
	auto atlas = ServiceManager::Get<RENDER::Renderer>().createTextureAtlas(descriptor, { bytes });
	mAtlases[cacheKey] = atlas;
	co_return atlas;
}

COROUTINE::Task<ResourcePtr<RENDER::TextureInterface>> TextureLoader::createResourceTask(
	std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	auto& fs = ServiceManager::Get<FileSystem>();
	RENDER::TextureResource descriptor;
	descriptor.path = path;
	descriptor.useMipmap = true;
	ConfigCandidate candidate;
	if (path.ends_with(".texture") && type == ELoadingType::RESOURCE) {
		candidate = BeginConfigLoad(path);
		if (!candidate.descriptor) {
			auto file = fs.getFile(path, FileMode::READ);
			if (!file || !file->isValid()) {
				throw std::runtime_error("Missing texture descriptor: " + path);
			}
			auto bytes = co_await file->readAllTask(control);
			ParseConfigCandidate(candidate, std::string(bytes.begin(), bytes.end()), path);
		}
		descriptor = *candidate.descriptor;
	}
	else {
		descriptor.pathTexture = { path };
		descriptor.isFloat = path.ends_with(".hdr");
	}
	std::vector<std::vector<uint8_t>> data;
	std::unordered_set<std::string> watches { path };
	for (const auto& source : descriptor.pathTexture) {
		control->check();
		auto file = fs.getFile(source, FileMode::READ);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Missing texture: " + source);
		}
		data.push_back(co_await file->readAllTask(control));
		watches.insert(source);
	}
	control->check();
	co_await COROUTINE::YieldIfBudgetExceeded();
	control->check();
	auto texture = ServiceManager::Get<RENDER::Renderer>().createTexture(descriptor, data, nullptr, createCacheDeleter(path));
	if (!texture) {
		throw std::runtime_error("Cannot create texture: " + path);
	}
	if (candidate.descriptor && !PublishConfig(path, candidate)) {
		throw ResourceLoadCancelled();
	}
	auto published = registerResource(path, std::move(texture), control.get());
	addFileWatchSubscribe(path, watches, published);
	co_return published;
}

TextureLoader::~TextureLoader() {
	sDefaultTexture.reset();
}

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

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromFile(
	const std::string& filepath, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto loaded = LoadFileData(filepath, generateMipmap, false);
	if (!loaded.valid()) {
		return GetDefaultTexture();
	}

	auto& render = ServiceManager::Get<RENDER::Renderer>();
	return render.createTexture(filepath, loaded.fileData, generateMipmap, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromResource(
	const std::string& resPath, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto config = LoadConfig(resPath);
	return CreateFromResource(config, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromResource(
	const RENDER::TextureResource& res, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto descriptor = res;
	auto fileData = readTextureSources(descriptor);
	return ServiceManager::Get<RENDER::Renderer>().createTexture(descriptor, fileData, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateAtlasFromFile(
	const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto loaded = LoadFileData(path, generateMipmap, false);
	if (!loaded.valid()) {
		return GetDefaultTexture();
	}
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	RENDER::TextureResource res;
	res.path = path;
	res.pathTexture = { path };
	res.useMipmap = generateMipmap;
	return render.createTextureAtlas(res, { loaded.fileData }, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromFileHDR(
	const std::string& filepath, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	auto loaded = LoadFileData(filepath, generateMipmap, true);
	if (!loaded.valid()) {
		return GetDefaultTexture();
	}
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	RENDER::TextureResource res;
	res.path = filepath;
	res.pathTexture = { filepath };
	res.useMipmap = generateMipmap;
	res.isFloat = true;
	return render.createTexture(res, { loaded.fileData }, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateColor(
	uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	RENDER::TextureResource res;
	res.colorData = { r, g, b, a };
	res.width = res.height = 1;
	res.channels = 4;
	res.pixelType = RENDER::PixelFormat::RGBA_INT;
	res.useMipmap = generateMipmap;
	auto& render = ServiceManager::Get<RENDER::Renderer>();
	return render.createTexture(res, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateColor(
	uint32_t data, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
	return CreateColor((data >> 24) & 0xFF, (data >> 16) & 0xFF, (data >> 8) & 0xFF, data & 0xFF, generateMipmap, allocator, deleter);
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::CreateFromMemory(
	const std::string& path, const std::vector<uint8_t>& data, bool generateMipmap, UTILS::IAllocator* allocator, RENDER::TextureDeleter deleter) {
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
	return createResource(path, type, std::any {});
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
	if (type == ELoadingType::RESOURCE) {
		if (data.has_value()) {
			if (auto* res = std::any_cast<RENDER::TextureResource>(&data)) {
				if (auto newResource = CreateFromResource(*res)) {
					return registerResource(path, newResource);
				}
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

	const auto key = NormalizePath(path);
	const auto candidate = ReadConfigCandidate(key);
	const auto& descriptor = *candidate.descriptor;
	if (auto resource = CreateFromResource(descriptor, nullptr, createCacheDeleter(key)); resource && PublishConfig(key, candidate)) {
		std::unordered_set<std::string> paths { key };
		paths.insert(descriptor.pathTexture.begin(), descriptor.pathTexture.end());
		addFileWatchSubscribe(key, paths, resource);
		return registerResource(key, resource);
	}
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createFromFile(const std::string& path, bool generateMipmap) {
	if (auto resource = getResource(path)) {
		return resource;
	}
	if (auto newResource = CreateFromFile(path, generateMipmap, nullptr, createCacheDeleter(path))) {

		{ // FileWatch
			auto& fs = ServiceManager::Get<FileSystem>();
			addFileWatchSubscribe(path, { path }, newResource);
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
		{ // FileWatch
			auto& fs = ServiceManager::Get<FileSystem>();
			if (auto basePathOpt = fs.getFilePath(path)) {
				addFileWatchSubscribe(*basePathOpt, { *basePathOpt }, newResource);
			}
		}
		return registerResource(path, newResource);
	}
	return GetDefaultTexture();
}

ResourcePtr<RENDER::TextureInterface> TextureLoader::createColor(
	const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool generateMipmap) {
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

ResourcePtr<RENDER::TextureInterface> TextureLoader::createFromMemory(
	const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap) {
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
	const auto cacheKey = std::to_string(ServiceManager::Get<FileSystem>().generation()) + ":" + NormalizePath(path);
	if (auto cached = mAtlases[cacheKey].lock()) {
		return cached;
	}
	if (auto newResource = CreateAtlasFromFile(path, generateMipmap)) {
		mAtlases[cacheKey] = newResource;
		return newResource;
	}
	ASSERT("TextureLoader::createAtlasFromFile — can not create texture");
	return GetDefaultTexture();
}

bool TextureLoader::reloadResource(std::weak_ptr<RENDER::TextureInterface> weakRes, const std::string& path) {
	if (auto res = weakRes.lock()) {
		res->requireReloadOwner();
		RENDER::TextureResource config;
		ConfigCandidate candidate;
		if (path.ends_with(".texture")) {
			candidate = ReadConfigCandidate(path, true);
			config = *candidate.descriptor;
		}
		else {
			config.path = path;
			config.pathTexture = { path };
			config.useMipmap = res->getUseMipMap();
			config.isFloat = path.ends_with(".hdr");
		}
		const auto data = readTextureSources(config);
		validateTextureReload(config, data);
		if (config.useMipmap && config.mipMapCount == 0) {
			config.mipMapCount = std::bit_width(static_cast<unsigned>(std::max(config.width, config.height)));
		}
		auto replacement = ServiceManager::Get<RENDER::Renderer>().createTexture(config, data);
		if (!replacement) {
			return false;
		}
		const auto swap = [&]() noexcept { return res->adoptReloadedTexture(*replacement); };
		if (!(candidate.descriptor ? CommitReload(path, candidate, swap) : swap())) {
			return false;
		}
		std::unordered_set<std::string> paths { path };
		paths.insert(config.pathTexture.begin(), config.pathTexture.end());
		addFileWatchSubscribe(path, paths, res);
		return true;
	}
	return false;
}

} // namespace IKIGAI::RESOURCES
