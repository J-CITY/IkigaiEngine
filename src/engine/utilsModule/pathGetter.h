#pragma once
#include <coreModule/config.h>
#include <filesystem>
#include <string>

namespace IKIGAI::UTILS {
// Legacy disk lookup. Android assets are read through SdlFileSystem.
static std::string GetRealPath(const std::string &p_path) {
  if (std::filesystem::exists(p_path)) {
    return p_path;
  }
  if (std::filesystem::exists(Config::ROOT + Config::ENGINE_ASSETS_PATH + p_path)) {
    return Config::ROOT + Config::ENGINE_ASSETS_PATH + p_path;
  }
  if (std::filesystem::exists(Config::ROOT + Config::USER_ASSETS_PATH + p_path)) {
    return Config::ROOT + Config::USER_ASSETS_PATH + p_path;
  }
  // TODO: throw or assert here
  return "";
}
} // namespace IKIGAI::UTILS
