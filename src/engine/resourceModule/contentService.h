#pragma once
#include "fileSystem/fileSystem.h"
#include "networkModule/downloadManager.h"
#include <nlohmann/json.hpp>

namespace IKIGAI::RESOURCES {
struct ContentManifest;
// Activation is a main-thread startup/scene-boundary operation. stageLocal and
// download are independent of the live VFS. The caller supplies a trusted manifest.
class ContentService {
	FileSystem& mFiles;
	std::filesystem::path mRoot;
	std::string mPlatform;
	std::vector<FileSystem::MountId> mMounts;
	std::string mRelease;
	ContentManifest validate(nlohmann::json manifest) const;
	void verify(const ContentManifest& manifest) const;

public:
	ContentService(FileSystem& files, std::filesystem::path root, std::string platform);
	const std::string& release() const {
		return mRelease;
	}
	void stageLocal(const std::filesystem::path& manifest);
	std::vector<NETWORK::DownloadHandle> download(
		NETWORK::DownloadManager& manager, const nlohmann::json& trustedManifest, const std::string& baseUrl, bool allowLocalhost = false);
	std::shared_future<void> activate(const nlohmann::json& manifest);
	bool restore();
};
}
