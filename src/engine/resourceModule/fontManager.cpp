#include "fontManager.h"
#include "utilsModule/memory/resourceAllocator.h"
#include <iostream>
#include <stdexcept>
namespace IKIGAI::RESOURCES {
namespace {
	struct LoadedFont {
		std::vector<uint8_t> bytes;
		VECTOR::FontOptions options;
		std::unordered_set<std::string> files;
	};
	LoadedFont readFont(const std::string& path) {
		auto& fs = ServiceManager::Get<FileSystem>();
		auto file = fs.getFile(path);
		if (!file || !file->isValid())
			throw std::runtime_error("SDF font asset not found: " + path);
		auto bytes = file->read();
		VECTOR::FontOptions options;
		std::unordered_set<std::string> paths;
		if (auto native = fs.getFilePath(path))
			paths.insert(*native);
		if (path.ends_with(".json") || path.ends_with(".font")) {
			auto config = nlohmann::json::parse(bytes);
			std::string font = config.at("File").get<std::string>();
			options.pixelSize = config.value("PixelSize", 48u);
			options.spread = config.value("Spread", 8u);
			options.pageSize = config.value("PageSize", 512u);
			options.fallback = config.value("Fallback", 0xFFFDu);
			file = fs.getFile(font);
			if (!file || !file->isValid())
				throw std::runtime_error("SDF font source not found: " + font);
			bytes = file->read();
			if (auto native = fs.getFilePath(font))
				paths.insert(*native);
		}
		return { std::move(bytes), options, std::move(paths) };
	}
}
ResourcePtr<VECTOR::FontResource> FontLoader::createResource(const std::string& path) {
	auto loaded = readFont(path);
	auto result = MEMORY::MakeShared<VECTOR::FontResource, MEMORY::Category::Font>(std::move(loaded.bytes), loaded.options);
	addFileWatchSubscribe(path, loaded.files, result);
	return result;
}
bool FontLoader::reloadResource(std::weak_ptr<VECTOR::FontResource> weak, const std::string& path) {
	if (auto font = weak.lock()) {
		try {
			auto data = readFont(path);
			font->reload(std::move(data.bytes), data.options);
			addFileWatchSubscribe(path, data.files, font);
			return true;
		} catch (const std::exception& e) {
			std::cerr << "SDF font reload: " << e.what() << '\n';
		}
	}
	return false;
}
}
