#pragma once
#include "aiModule/blackboard/blackboard.h"
#include "resourceManager.h"

namespace IKIGAI::RESOURCES {
struct BlackboardResource {
	std::shared_ptr<const AI::BlackboardSchema> schema;
	uint64_t revision = 0;
};
class BlackboardLoader : public ResourceManager<BlackboardResource> {
public:
	static std::shared_ptr<const AI::BlackboardSchema> compileFile(const std::string&);
	bool reload(const std::string&);

private:
	ResourcePtr<BlackboardResource> createResource(const std::string&) override;
	bool reloadResource(std::weak_ptr<BlackboardResource>, const std::string&) override;
};
}
