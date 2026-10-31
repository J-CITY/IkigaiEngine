#pragma once
#if USE_PHYSICS_3D
#include "destructionModule/destructibleDefinition.h"
#include "resourceManager.h"
namespace IKIGAI::RENDER {
class MaterialInterface;
}
namespace IKIGAI::RESOURCES {
struct DestructibleResource {
	std::shared_ptr<const DESTRUCTION::DestructibleDefinition> definition {};
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> materials {};
	std::uint64_t revision = 0;
	std::unordered_set<std::string> dependencies {};
};
class DestructibleLoader : public ResourceManager<DestructibleResource> {
private:
	void addFileWatchSubscribe(
		const std::string& path, const std::unordered_set<std::string>& paths, std::weak_ptr<DestructibleResource> resource) override;
	ResourcePtr<DestructibleResource> createResource(const std::string& path) override;
	bool reloadResource(std::weak_ptr<DestructibleResource> resource, const std::string& path) override;
};
}
#endif
