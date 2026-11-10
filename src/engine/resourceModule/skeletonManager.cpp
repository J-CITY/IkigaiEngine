#include "skeletonManager.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "skeletalModule/skeleton.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include <resourceModule/fileSystem/fileSystem.h>


namespace IKIGAI::RESOURCES {

AssimpParser SkeletonLoader::_ASSIMP;

ResourcePtr<SKELETON::Skeleton> SkeletonLoader::CreateFromFile(const std::string& path, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	auto skeleton = MEMORY::MakeSharedWithDeleter<SKELETON::Skeleton, MEMORY::Category::Skeleton>(allocator, deleter);
	auto file = ServiceManager::Get<FileSystem>().getFile(path);
	auto data = file && file->isValid() ? file->read() : std::vector<uint8_t> {};
	if (data.empty()) {
		throw std::runtime_error("Missing or empty skeleton source: " + path);
	}
	if (!_ASSIMP.LoadSkeleton(path, data, *skeleton)) {
		throw std::runtime_error("Cannot import skeleton: " + path);
	}

	return skeleton;
}

ResourcePtr<SKELETON::Skeleton> SkeletonLoader::CreateFromResource(const std::string& path, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	RENDER::SkeletonResource res = LoadConfig(path);
	return CreateFromResource(res, allocator, deleter);
}

ResourcePtr<SKELETON::Skeleton> SkeletonLoader::CreateFromResource(
	const RENDER::SkeletonResource& res, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	return CreateFromFile(res.pathSkeleton, allocator, deleter);
}

ResourcePtr<SKELETON::Skeleton> SkeletonLoader::createResource(const std::string& path) {
	if (path.ends_with(".skeleton")) {
		return createResource(path, ELoadingType::RESOURCE);
	}
	return createResource(path, ELoadingType::FILE);
}

ResourcePtr<SKELETON::Skeleton> SkeletonLoader::createResource(const std::string& path, ELoadingType type) {
	return createResource(path, type, std::any());
}

ResourcePtr<SKELETON::Skeleton> SkeletonLoader::createResource(const std::string& path, ELoadingType type, std::any /*data*/) {
	ResourcePtr<SKELETON::Skeleton> res;
	if (type == ELoadingType::FILE) {
		res = CreateFromFile(path);
		addFileWatchSubscribe(path, { path }, res);
	}
	else if (type == ELoadingType::RESOURCE) {
		const auto candidate = ReadConfigCandidate(path);
		const auto& config = *candidate.descriptor;
		res = CreateFromResource(config);
		if (res && !PublishConfig(path, candidate)) {
			return nullptr;
		}
		addFileWatchSubscribe(path, { path, config.pathSkeleton }, res);
	}
	return res;
}

bool SkeletonLoader::reloadResource(std::weak_ptr<SKELETON::Skeleton> weakRes, const std::string& path) {
	if (auto res = weakRes.lock()) {
		std::unordered_set<std::string> paths = { path };
		RENDER::SkeletonResource config;
		ConfigCandidate candidate;
		if (path.ends_with(".skeleton")) {
			candidate = ReadConfigCandidate(path, true);
			config = *candidate.descriptor;
			paths.insert(config.pathSkeleton);
		}
		else {
			config.path = path;
			config.pathSkeleton = path;
		}

		auto newSkeleton = CreateFromFile(config.pathSkeleton);
		if (!newSkeleton) {
			return false;
		}
		const auto swap = [&]() noexcept {
			res->setNumJoints(newSkeleton->getNumJolts());
			res->joints().swap(newSkeleton->joints());
		};
		if (candidate.descriptor) {
			if (!CommitReload(path, candidate, swap)) {
				return false;
			}
		}
		else {
			swap();
		}

		addFileWatchSubscribe(path, paths, res);
		return true;
	}
	return false;
}

} // namespace IKIGAI::RESOURCES
