#pragma once

#include "atlasInterface.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
struct TextureAssetSource {
	std::vector<std::uint8_t> (*readImage)(const std::string&) = nullptr;
	std::optional<AtlasData> (*readAtlas)(const std::string&) = nullptr;
};

std::vector<std::uint8_t> ReadTextureImage(const std::string& path);
std::optional<AtlasData> ReadTextureAtlas(const std::string& imagePath);
}
