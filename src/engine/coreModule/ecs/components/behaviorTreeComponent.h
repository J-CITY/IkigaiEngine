#pragma once
#include "aiModule/behaviorTree/behaviorTree.h"
#include "component.h"
#include "coreModule/ecs/componentReference.h"

namespace IKIGAI::ECS {
class BehaviorTreeComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::string Tree {};
		ComponentReference Motor = {};
		template <class C> static auto serde(C& c, Descriptor& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v)
				.field(&Descriptor::Type, "BehaviorTreeComponentType")
				.field(&Descriptor::Tree, "Tree")
				.field(&Descriptor::Motor, "Motor", default_ { ComponentReference {} });
		}
	};
	explicit BehaviorTreeComponent(UTILS::Ref<Object>);
	BehaviorTreeComponent(UTILS::Ref<Object>, const ComponentBase::Descriptor&);
	~BehaviorTreeComponent() override;
	BehaviorTreeComponent(BehaviorTreeComponent&&) = default;
	BehaviorTreeComponent& operator=(BehaviorTreeComponent&&) = default;
	BehaviorTreeComponent(const BehaviorTreeComponent&) = delete;
	BehaviorTreeComponent& operator=(const BehaviorTreeComponent&) = delete;
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void applyCommands(SCENE_SYSTEM::Scene&);
	void update(SCENE_SYSTEM::Scene&, double dt);
	void pause(bool);
	void stop();
	void restart();
	AI::BehaviorTreeInstance* instance() const;
	nlohmann::json diagnostics() const;
	void onDisable() override;
	void onDestroy() override;
	Descriptor settings {};

private:
	struct State;
	std::shared_ptr<State> mState {};
	bool mPaused = false;
	bool mStopped = false;
	std::string mError {};
};
template <> inline std::string GetType<BehaviorTreeComponent>() {
	return "class IKIGAI::ECS::BehaviorTreeComponent";
}
template <> inline std::string GetComponentName<BehaviorTreeComponent>() {
	return "BehaviorTreeComponent";
}
}
