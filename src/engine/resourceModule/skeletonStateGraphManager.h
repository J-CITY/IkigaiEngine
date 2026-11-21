#pragma once

#include "resourceManager.h"
#include "skeletalModule/skeletalStateGraph.h"
#include "utilsModule/memory/allocator.h"

namespace IKIGAI::RESOURCES {
class SkeletonStateGraphLoader : public ResourceManager<SKELETON::SkeletalStateGraph> {
public:
	using ResourceDeleter = std::function<void(SKELETON::SkeletalStateGraph*)>;
	static ResourcePtr<SKELETON::SkeletalStateGraph> CreateFromResource(
		const std::string& path, UTILS::IAllocator* allocator = nullptr, ResourceDeleter deleter = nullptr);

private:
	static std::shared_ptr<const SKELETON::StateGraphDefinition> CompileDefinition(nlohmann::json& document);
	void addFileWatchSubscribe(const std::string&, const std::unordered_set<std::string>&, std::weak_ptr<SKELETON::SkeletalStateGraph>) override;
	ResourcePtr<SKELETON::SkeletalStateGraph> createResource(const std::string& path) override;
	ResourcePtr<SKELETON::SkeletalStateGraph> createResource(const std::string& path, ELoadingType type) override;
	ResourcePtr<SKELETON::SkeletalStateGraph> createResource(const std::string& path, ELoadingType type, std::any data) override;

	bool reloadResource(std::weak_ptr<SKELETON::SkeletalStateGraph> weakRes, const std::string& path) override;
};
}
