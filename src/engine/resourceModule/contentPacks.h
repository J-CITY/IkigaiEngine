#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct android_app;

namespace IKIGAI::RESOURCES {
class FileSystem;

// Mount assets/engine, then every assets_packs/<id> directory, then assets/game.
// Each pack root is mounted at /, so virtual paths stay /shaders, /materials, /models.
// On Android and Quest the packaged tree lists packs in /content_packs.json and
// stores them under packs/<id> inside the APK assets root.
void MountLooseAssets(FileSystem& files, const std::string& root, android_app* androidApp);

std::vector<std::filesystem::path> ContentPackRoots(const std::string& root);

// Disk lookup that follows mount precedence: game, then packs, then engine.
std::string FindLooseAsset(const std::string& relative);
}
