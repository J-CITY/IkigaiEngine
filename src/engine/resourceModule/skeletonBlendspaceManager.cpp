#include "skeletonBlendspaceManager.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "skeletalModule/animation.h"
#include "skeletalModule/blendspace.h"
#include "skeletalModule/skeleton.h"
#include "skeletonAnimationManager.h"
#include "skeletonManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include <iostream>
#include <resourceModule/fileSystem/fileSystem.h>
#include <stdexcept>

namespace IKIGAI::RESOURCES {

// ---------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------

ResourcePtr<SKELETON::BlendspaceInterface> SkeletonBlendspaceLoader::CreateBlendspace1D(
	const RENDER::SkeletonBlendspace& desc, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	if (desc.nodes.empty() || desc.nodes[0].nodes.empty()) {
		throw std::runtime_error("Empty 1D blendspace");
	}
	auto skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(desc.skeletonPath);
	if (!skeleton) {
		throw std::runtime_error("Cannot load blendspace skeleton: " + desc.skeletonPath);
	}

	auto bs = MEMORY::MakeSharedWithDeleter<SKELETON::Blendspace1D, MEMORY::Category::Skeleton>(allocator, deleter);
	bs->mSkeleton = skeleton;
	bs->mNodes.reserve(desc.nodes[0].nodes.size());

	for (const auto& [value, animPath] : desc.nodes[0].nodes) {
		AnimationLoadContext ctx;
		ctx.skeleton = skeleton;
		auto anim = ServiceManager::Get<SkeletonAnimationLoader>().loadResource(animPath, ELoadingType::RESOURCE, ctx);
		if (!anim) {
			throw std::runtime_error("Cannot load blendspace animation: " + animPath);
		}
		bs->mNodes.push_back(SKELETON::Blendspace1D::Node { value, anim });
	}

	return bs;
}

ResourcePtr<SKELETON::BlendspaceInterface> SkeletonBlendspaceLoader::CreateBlendspace2D(
	const RENDER::SkeletonBlendspace& desc, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	auto skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(desc.skeletonPath);
	if (!skeleton) {
		throw std::runtime_error("Cannot load blendspace skeleton: " + desc.skeletonPath);
	}

	auto bs = MEMORY::MakeSharedWithDeleter<SKELETON::Blendspace2D, MEMORY::Category::Skeleton>(allocator, deleter);
	bs->mSkeleton = skeleton;
	bs->mRows.reserve(desc.nodes.size());

	for (const auto& descRow : desc.nodes) {
		SKELETON::Blendspace2D::Row row;
		row.value = descRow.value;
		for (const auto& [nodeValue, animPath] : descRow.nodes) {
			AnimationLoadContext ctx;
			ctx.skeleton = skeleton;
			auto anim = ServiceManager::Get<SkeletonAnimationLoader>().loadResource(animPath, ELoadingType::RESOURCE, ctx);
			if (!anim) {
				throw std::runtime_error("Cannot load blendspace animation: " + animPath);
			}
			row.nodes.push_back(SKELETON::Blendspace2D::Node { nodeValue, anim });
		}
		bs->mRows.push_back(std::move(row));
	}

	return bs;
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

ResourcePtr<SKELETON::BlendspaceInterface> SkeletonBlendspaceLoader::CreateFromResource(
	const std::string& path, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	// Distinguish 1D vs 2D by file extension
	const bool is1D = path.ends_with(".blendspace1d");
	const bool is2D = path.ends_with(".blendspace2d");

	if (is1D) {
		auto res = LoadConfig(path);
		auto variant = CreateBlendspace1D(res, allocator, deleter);
		return variant;
	}

	if (is2D) {
		auto res = LoadConfig(path);
		auto variant = CreateBlendspace2D(res, allocator, deleter);
		return variant;
	}

	throw std::runtime_error("Unknown blendspace extension: " + path);
}

ResourcePtr<SKELETON::BlendspaceInterface> SkeletonBlendspaceLoader::createResource(const std::string& path) {
	return createResource(path, ELoadingType::RESOURCE);
}

ResourcePtr<SKELETON::BlendspaceInterface> SkeletonBlendspaceLoader::createResource(const std::string& path, ELoadingType /*type*/) {
	return createResource(path, ELoadingType::RESOURCE, std::any {});
}

ResourcePtr<SKELETON::BlendspaceInterface> SkeletonBlendspaceLoader::createResource(const std::string& path, ELoadingType type, std::any /*data*/) {
	if (type == ELoadingType::RESOURCE) {
		const auto candidate = ReadConfigCandidate(path);
		const auto& config = *candidate.descriptor;

		ResourcePtr<SKELETON::BlendspaceInterface> res;
		if (path.ends_with(".blendspace1d")) {
			res = CreateBlendspace1D(config, nullptr, createCacheDeleter(path));
		}
		else if (path.ends_with(".blendspace2d")) {
			res = CreateBlendspace2D(config, nullptr, createCacheDeleter(path));
		}
		else {
			throw std::runtime_error("Unknown blendspace extension: " + path);
		}
		if (res && !PublishConfig(path, candidate)) {
			return nullptr;
		}
		addFileWatchSubscribe(path, { path }, res);
		return res;
	}
	return nullptr;
}

bool SkeletonBlendspaceLoader::reloadResource(std::weak_ptr<SKELETON::BlendspaceInterface> weakRes, const std::string& path) {
	if (auto res = weakRes.lock()) {
		try {
			const auto config = ReadConfigCandidate(path, true);
			if (auto target = std::dynamic_pointer_cast<SKELETON::Blendspace1D>(res)) {
				auto candidate = std::static_pointer_cast<SKELETON::Blendspace1D>(CreateBlendspace1D(*config.descriptor));
				SKELETON::Blendspace1DInstance validate(candidate);
				candidate->revision = target->revision + 1;
				return CommitReload(path, config, [&]() noexcept { std::swap(*target, *candidate); });
			}
			else if (auto target = std::dynamic_pointer_cast<SKELETON::Blendspace2D>(res)) {
				auto candidate = std::static_pointer_cast<SKELETON::Blendspace2D>(CreateBlendspace2D(*config.descriptor));
				SKELETON::Blendspace2DInstance validate(candidate);
				candidate->revision = target->revision + 1;
				return CommitReload(path, config, [&]() noexcept { std::swap(*target, *candidate); });
			}
		} catch (const std::exception& e) {
			std::cerr << "Blendspace reload rejected: " << e.what() << std::endl;
			return false;
		}
	}
	return false;
}
} // namespace IKIGAI::RESOURCES
