#pragma once
#include "renderModule/backends/interface/atlasInterface.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/backends/interface/resourceStruct.h"
#include "resourceManager.h"
#include "resourceResolver.h"
#include "utilsModule/assertion.h"
#include "utilsModule/event.h"
#include "utilsModule/memory/memory.h"
#include <any>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace IKIGAI::RENDER {
class TextureInterface;
}

namespace IKIGAI::RESOURCES {

class TextureLoader : public ResourceManager<RENDER::TextureInterface, RENDER::TextureResource> {
public:
	static std::vector<std::uint8_t> ReadImage(const std::string& path);
	static std::optional<RENDER::AtlasData> ReadAtlas(const std::string& imagePath);
	TextureLoader();
	ResolvedTexture resolveSprite(const std::string& path);
	ResourceRequest<RENDER::TextureInterface> requestSprite(const std::string& path);
	ResourcePtr<RENDER::TextureInterface> createSprite(const std::string& path);
	ResourceRequest<RENDER::TextureInterface> requestAtlas(std::string path);
	~TextureLoader() override;

	// С кэшем и FileWatch
	ResourcePtr<RENDER::TextureInterface> createFromResource(const std::string& path);
	ResourcePtr<RENDER::TextureInterface> createFromFile(const std::string& path, bool generateMipmap);
	ResourcePtr<RENDER::TextureInterface> createAtlasFromFile(const std::string& path, bool generateMipmap);
	ResourcePtr<RENDER::TextureInterface> createFromFileHDR(const std::string& path, bool generateMipmap);
	ResourcePtr<RENDER::TextureInterface> createColor(const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool generateMipmap);
	ResourcePtr<RENDER::TextureInterface> createColor(const std::string& name, uint32_t data, bool generateMipmap);
	ResourcePtr<RENDER::TextureInterface> createFromMemory(const std::string& name, const std::vector<uint8_t>& data, bool generateMipmap);

	// Без кэша и FileWatch — static
	static ResourcePtr<RENDER::TextureInterface> CreateFromFile(
		const std::string& filepath, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateFromResource(
		const std::string& resPath, UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateFromResource(
		const RENDER::TextureResource& res, UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateAtlasFromFile(
		const std::string& path, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateFromFileHDR(
		const std::string& filepath, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool generateMipmap,
		UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateColor(
		uint32_t p_data, bool generateMipmap, UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);
	static ResourcePtr<RENDER::TextureInterface> CreateFromMemory(const std::string& path, const std::vector<uint8_t>& data, bool generateMipmap,
		UTILS::IAllocator* allocator = nullptr, RENDER::TextureDeleter deleter = nullptr);

private:
	std::unique_ptr<ResourceResolver> mResolver;
	std::unordered_map<std::string, std::weak_ptr<RENDER::TextureInterface>> mAtlases;
	COROUTINE::Task<ResourcePtr<RENDER::TextureInterface>> createAtlasTask(std::string path, std::shared_ptr<ResourceLoadControl> control);
	COROUTINE::Task<ResourcePtr<RENDER::TextureInterface>> createResourceTask(
		std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) override;
	struct LoadedTextureData {
		RENDER::TextureResource descriptor;
		std::vector<uint8_t> fileData;
		bool valid() const {
			return !fileData.empty();
		}
	};
	static LoadedTextureData LoadFileData(const std::string& realPath, bool generateMipmap, bool isHDR = false);

	ResourcePtr<RENDER::TextureInterface> createResource(const std::string& path) override;
	ResourcePtr<RENDER::TextureInterface> createResource(const std::string& path, ELoadingType type) override;
	ResourcePtr<RENDER::TextureInterface> createResource(const std::string& path, ELoadingType type, std::any data) override;

	// Дефолтная текстура (розовый 1x1 пиксель)
	static ResourcePtr<RENDER::TextureInterface> GetDefaultTexture();
	static void InitDefaultTexture();

	// static void AddToFileWatch(const std::string& basePath,
	//	const std::string& watchPath,
	//	std::function<RENDER::TextureResource()> reloadFn,
	//	std::weak_ptr<RENDER::TextureInterface> weakTex);
	virtual bool reloadResource(std::weak_ptr<RENDER::TextureInterface> weakRes, const std::string& config) override;

	// Дефолтная розовая текстура-фоллбэк (горячий розовый 1×1)
	inline static ResourcePtr<RENDER::TextureInterface> sDefaultTexture;
};

} // namespace IKIGAI::RESOURCES
