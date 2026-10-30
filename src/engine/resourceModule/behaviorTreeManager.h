#pragma once
#include "aiModule/behaviorTree/behaviorTree.h"
#include "resourceManager.h"

namespace IKIGAI::RESOURCES {
struct BehaviorTreeResource {
	std::shared_ptr<const AI::BehaviorTreeDefinition> definition;
	uint64_t revision = 0;
};
class BehaviorTreeLoader : public ResourceManager<BehaviorTreeResource> {
public:
	static std::shared_ptr<const AI::BehaviorTreeDefinition> compileFile(const std::string&);
	bool reload(const std::string&);

private:
	ResourcePtr<BehaviorTreeResource> createResource(const std::string&) override;
	void addFileWatchSubscribe(const std::string&, const std::unordered_set<std::string>&, std::weak_ptr<BehaviorTreeResource>) override;
	bool reloadResource(std::weak_ptr<BehaviorTreeResource>, const std::string&) override;
};
}
