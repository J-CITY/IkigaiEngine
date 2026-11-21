#include "textureAssetSource.h"
#include "utilsModule/serviceManager.h"

namespace IKIGAI::RENDER {
std::vector<std::uint8_t> ReadTextureImage(const std::string& path) {
	if (UTILS::ServiceManager::Check<const TextureAssetSource>()) {
		const auto read = UTILS::ServiceManager::Get<const TextureAssetSource>().readImage;
		if (read) {
			return read(path);
		}
	}
	return {};
}

std::optional<AtlasData> ReadTextureAtlas(const std::string& imagePath) {
	if (UTILS::ServiceManager::Check<const TextureAssetSource>()) {
		const auto read = UTILS::ServiceManager::Get<const TextureAssetSource>().readAtlas;
		if (read) {
			return read(imagePath);
		}
	}
	return {};
}
}
