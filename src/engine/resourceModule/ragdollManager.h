#pragma once
#if USE_PHYSICS_3D
#include "ragdollModule/ragdollDefinition.h"
#include "resourceManager.h"

namespace IKIGAI::RESOURCES {
struct RagdollResource {
	std::shared_ptr<const SKELETON::RagdollDefinition> definition {};
	std::uint64_t revision = 0;
};
class RagdollLoader : public ResourceManager<RagdollResource> {
public:
	static std::shared_ptr<const SKELETON::RagdollDefinition> compileFile(const std::string&);
	bool reload(const std::string&);

private:
	ResourcePtr<RagdollResource> createResource(const std::string&) override;
	bool reloadResource(std::weak_ptr<RagdollResource>, const std::string&) override;
};
}
#endif
