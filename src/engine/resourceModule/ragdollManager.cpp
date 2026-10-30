#include "ragdollManager.h"
#if USE_PHYSICS_3D
#include "skeletonManager.h"
#include "utilsModule/serialization/typedDescriptor.h"
#include <iostream>

namespace IKIGAI::RESOURCES {
std::shared_ptr<const SKELETON::RagdollDefinition> RagdollLoader::compileFile(const std::string& path) {
	try {
		auto file = ServiceManager::Get<FileSystem>().getFile(path);
		if (!file || !file->isValid()) {
			throw std::runtime_error("missing file");
		}
		auto descriptor = DATA::readDescriptor<SKELETON::RagdollDescriptor>(nlohmann::json::parse(file->readStr()));
		if (descriptor.Skeleton.empty()) {
			throw std::runtime_error("empty Skeleton path");
		}
		auto skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(descriptor.Skeleton, ELoadingType::FILE);
		return SKELETON::RagdollDefinition::compile(descriptor, skeleton);
	} catch (const std::exception& error) {
		throw std::runtime_error("Ragdoll " + path + ": " + error.what());
	}
}
ResourcePtr<RagdollResource> RagdollLoader::createResource(const std::string& path) {
	auto result = std::make_shared<RagdollResource>();
	result->definition = compileFile(path);
	addFileWatchSubscribe(path, { path }, result);
	return result;
}
bool RagdollLoader::reloadResource(std::weak_ptr<RagdollResource> weak, const std::string& path) {
	if (auto result = weak.lock()) {
		try {
			auto candidate = compileFile(path);
			result->definition = std::move(candidate);
			++result->revision;
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Ragdoll reload rejected: " << error.what() << '\n';
		}
	}
	return false;
}
bool RagdollLoader::reload(const std::string& path) {
	return reloadResource(loadResource(path), NormalizePath(path));
}
}
#endif
