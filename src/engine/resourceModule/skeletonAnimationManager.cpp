#include "skeletonAnimationManager.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "skeletalModule/animation.h"
#include "skeletalModule/skeleton.h"
#include "skeletonManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include <resourceModule/fileSystem/fileSystem.h>
#include <stdexcept>

namespace IKIGAI::RESOURCES {

AssimpParser SkeletonAnimationLoader::_ASSIMP;

ResourcePtr<SKELETON::Animation> SkeletonAnimationLoader::CreateFromFile(const std::string& path, std::shared_ptr<SKELETON::Skeleton> skeleton,
	bool additive, SKELETON::Animation* additiveReference, UTILS::IAllocator* allocator, ResourceDeleter deleter) {

	if (!skeleton) {
		throw std::runtime_error("SkeletonAnimationLoader: skeleton is null for " + path);
	}

	auto animation = MEMORY::MakeSharedWithDeleter<SKELETON::Animation, MEMORY::Category::Skeleton>(allocator, deleter);
	auto file = ServiceManager::Get<FileSystem>().getFile(path);
	auto data = file && file->isValid() ? file->read() : std::vector<uint8_t> {};
	if (data.empty()) {
		throw std::runtime_error("SkeletonAnimationLoader: empty file: " + path);
	}
	if (!_ASSIMP.LoadAnimation(path, data, *skeleton, *animation, additive, additiveReference)) {
		throw std::runtime_error("SkeletonAnimationLoader: failed to parse: " + path);
	}

	return animation;
}

ResourcePtr<SKELETON::Animation> SkeletonAnimationLoader::CreateFromResource(
	const std::string& path, std::shared_ptr<SKELETON::Skeleton> skeleton, UTILS::IAllocator* allocator, ResourceDeleter deleter) {

	RENDER::AnimationResource res = LoadConfig(path);
	return CreateFromResource(res, skeleton, allocator, deleter);
}

ResourcePtr<SKELETON::Animation> SkeletonAnimationLoader::CreateFromResource(
	const RENDER::AnimationResource& res, std::shared_ptr<SKELETON::Skeleton> skeleton, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	// Resolve additive reference if specified
	SKELETON::Animation* additiveRef = nullptr;
	std::shared_ptr<SKELETON::Animation> additiveRefPtr;
	if (!res.additiveReference.empty()) {
		additiveRefPtr = ServiceManager::Get<SkeletonAnimationLoader>().loadResource(res.additiveReference);
		if (!additiveRefPtr) {
			throw std::runtime_error("Missing additive reference animation: " + res.additiveReference);
		}
		additiveRef = additiveRefPtr.get();
	}

	auto animation = CreateFromFile(res.pathAnimation, skeleton, res.additive, additiveRef, allocator, deleter);
	return animation;
}

ResourcePtr<SKELETON::Animation> SkeletonAnimationLoader::createResource(const std::string& path) {
	return createResource(path, ELoadingType::RESOURCE, std::any());
}

ResourcePtr<SKELETON::Animation> SkeletonAnimationLoader::createResource(const std::string& path, ELoadingType type) {
	return createResource(path, type, std::any());
}

ResourcePtr<SKELETON::Animation> SkeletonAnimationLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
	std::shared_ptr<SKELETON::Skeleton> skeleton;
	bool additive = false;
	std::string additiveRefPath;

	if (data.has_value()) {
		try {
			auto ctx = std::any_cast<AnimationLoadContext>(data);
			skeleton = ctx.skeleton;
			additive = ctx.additive;
			additiveRefPath = ctx.additiveReferencePath;
		} catch (const std::bad_any_cast&) {
			ASSERT("SkeletonAnimationLoader: invalid std::any context, expected AnimationLoadContext");
			return nullptr;
		}
	}

	ResourcePtr<SKELETON::Animation> res;
	if (type == ELoadingType::FILE) {
		if (!skeleton) {
			ASSERT("SkeletonAnimationLoader: AnimationLoadContext.skeleton is null");
			return nullptr;
		}
		res = CreateFromFile(path, skeleton, additive, nullptr, nullptr, createCacheDeleter(path));
		addFileWatchSubscribe(path, { path }, res);
	}
	else if (type == ELoadingType::RESOURCE) {
		const auto candidate = ReadConfigCandidate(path);
		const auto& config = *candidate.descriptor;
		skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(
			config.skeletonPath, config.skeletonPath.ends_with(".skeleton") ? ELoadingType::RESOURCE : ELoadingType::FILE);
		if (!skeleton) {
			throw std::runtime_error("Missing animation skeleton: " + config.skeletonPath);
		}
		res = CreateFromResource(config, skeleton, nullptr, createCacheDeleter(path));
		if (res && !PublishConfig(path, candidate)) {
			return nullptr;
		}
		addFileWatchSubscribe(path, { path, config.pathAnimation }, res);
	}
	return res;
}

bool SkeletonAnimationLoader::reloadResource(std::weak_ptr<SKELETON::Animation> weakRes, const std::string& path) {
	if (auto res = weakRes.lock(); res && path.ends_with(".animation")) {
		const auto candidate = ReadConfigCandidate(path, true);
		const auto& config = *candidate.descriptor;
		if (!config.additiveReference.empty() && NormalizePath(config.additiveReference) == path) {
			throw std::runtime_error("Animation references itself: " + path);
		}
		auto skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(
			config.skeletonPath, config.skeletonPath.ends_with(".skeleton") ? ELoadingType::RESOURCE : ELoadingType::FILE);
		auto replacement = CreateFromResource(config, skeleton);
		if (!replacement || !CommitReload(path, candidate, [&]() noexcept { std::swap(*res, *replacement); })) {
			return false;
		}
		addFileWatchSubscribe(path, { path, config.pathAnimation }, res);
		return true;
	}
	return false;
}
} // namespace IKIGAI::RESOURCES
