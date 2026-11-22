#include "contentPacks.h"

#include "fileSystem/fileSystem.h"
#include "utilsModule/serialization/jsonAdapters.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#ifdef OCULUS
#include <android_native_app_glue.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace IKIGAI::RESOURCES {
namespace {
	std::filesystem::path PackRoot(const std::string& root) {
		return std::filesystem::u8path(root + "assets_packs");
	}

	bool HasPackDescriptor(const std::filesystem::path& directory) {
		std::error_code error;
		return std::filesystem::is_regular_file(directory / "pack.json", error);
	}

	std::vector<std::string> ReadPackagedPacks(FileSystem& files) {
		if (!files.isFileExist("/content_packs.json")) {
			return {};
		}
		const auto file = files.getFile("/content_packs.json");
		if (!file || !file->isValid()) {
			throw std::runtime_error("Cannot read /content_packs.json");
		}
		auto json = nlohmann::json::parse(file->readStr());
		if (!json.is_array()) {
			throw std::runtime_error("/content_packs.json must be an array of pack ids");
		}
		auto ids = serde::deserialize<std::vector<std::string>>(json);
		if (std::ranges::find(ids, std::string {}) != ids.end()) {
			throw std::runtime_error("/content_packs.json contains an empty pack id");
		}
		return ids;
	}

	void MountPackPath(FileSystem& files, const std::string& path, android_app* androidApp) {
#ifdef OCULUS
		if (!androidApp || !androidApp->activity) {
			throw std::runtime_error("Quest content pack mount has no android_app");
		}
		files.addAndroidAssetFileSystem(path, "/", androidApp->activity->assetManager, androidApp);
#elif defined(__ANDROID__)
		(void)androidApp;
		files.addSdlFileSystem(path, "/");
#else
		(void)androidApp;
		files.addNativeFileSystem(path, "/");
#endif
	}
}

void MountLooseAssets(FileSystem& files, const std::string& root, android_app* androidApp) {
#if defined(OCULUS) || defined(__ANDROID__)
	(void)root;
	MountPackPath(files, "engine", androidApp);
	for (const auto& id : ReadPackagedPacks(files)) {
		MountPackPath(files, "packs/" + id, androidApp);
	}
	MountPackPath(files, "game", androidApp);
#else
	(void)androidApp;
#if defined(__APPLE__) && TARGET_OS_IPHONE
	files.addSdlFileSystem(root + "assets/engine/", "/", true);
	for (const auto& pack : ContentPackRoots(root)) {
		files.addSdlFileSystem(pack.generic_string(), "/", true);
	}
	files.addSdlFileSystem(root + "assets/game/", "/", true);
#else
	files.addNativeFileSystem(root + "assets/engine/", "/");
	for (const auto& pack : ContentPackRoots(root)) {
		files.addNativeFileSystem(pack.generic_string(), "/");
	}
	files.addNativeFileSystem(root + "assets/game/", "/");
#endif
#endif
}

std::vector<std::filesystem::path> ContentPackRoots(const std::string& root) {
	std::vector<std::filesystem::path> packs;
	const auto directory = PackRoot(root);
	std::error_code error;
	if (!std::filesystem::is_directory(directory, error)) {
		return packs;
	}
	for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
		if (error || !entry.is_directory()) {
			continue;
		}
		if (HasPackDescriptor(entry.path())) {
			packs.push_back(entry.path());
		}
	}
	std::sort(packs.begin(), packs.end());
	return packs;
}

std::string FindLooseAsset(const std::string& root, const std::string& relative) {
	std::string trimmed = relative;
	while (!trimmed.empty() && (trimmed.front() == '/' || trimmed.front() == '\\')) {
		trimmed.erase(trimmed.begin());
	}
	const auto candidate = [&](const std::filesystem::path& directory) {
		const auto path = directory / std::filesystem::u8path(trimmed);
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) ? path.lexically_normal().string() : std::string {};
	};
	if (const auto game = candidate(std::filesystem::u8path(root) / "assets" / "game"); !game.empty()) {
		return game;
	}
	auto packs = ContentPackRoots(root);
	std::reverse(packs.begin(), packs.end());
	for (const auto& pack : packs) {
		if (const auto found = candidate(pack); !found.empty()) {
			return found;
		}
	}
	return candidate(std::filesystem::u8path(root) / "assets" / "engine");
}
}
