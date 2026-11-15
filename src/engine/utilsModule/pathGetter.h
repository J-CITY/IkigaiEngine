#pragma once
#include "resourceModule/contentPacks.h"
#include <filesystem>
#include <string>

namespace IKIGAI::UTILS {
// Legacy disk lookup. Android assets are read through SdlFileSystem.
// Game overrides packs, packs override assets/engine, matching mount order.
static std::string GetRealPath(const std::string& p_path) {
	if (std::filesystem::exists(p_path)) {
		return p_path;
	}
	return RESOURCES::FindLooseAsset(p_path);
}
} // namespace IKIGAI::UTILS
