#pragma once
#include "aiModule/blackboard/blackboard.h"
#include "component.h"
#include "coreModule/ecs/componentReference.h"
namespace IKIGAI::ECS {
class VariableContextComponent;
class BlackboardComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::string Schema {};
		ComponentReference Context = {};
		std::map<std::string, std::string> Bindings {};
		template <class C> static auto serde(C& c, Descriptor& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v)
				.field(&Descriptor::Type, "BlackboardComponentType")
				.field(&Descriptor::Schema, "Schema")
				.field(&Descriptor::Context, "Context", default_ { ComponentReference {} })
				.field(&Descriptor::Bindings, "Bindings", default_ { std::map<std::string, std::string> {} });
		}
	};
	struct Prepared {
		std::shared_ptr<AI::Blackboard> board {};
		UTILS::WeakPtr<ComponentBase> owner {};
		bool current() const;
	};
	explicit BlackboardComponent(UTILS::Ref<Object>);
	BlackboardComponent(UTILS::Ref<Object>, const ComponentBase::Descriptor&);
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	std::shared_ptr<Prepared> prepare(SCENE_SYSTEM::Scene&, std::shared_ptr<const AI::BlackboardSchema>) const;
	static void synchronize(SCENE_SYSTEM::Scene&, Prepared&);
	void install(std::shared_ptr<Prepared>);
	std::shared_ptr<AI::Blackboard> blackboard() const;
	void onDestroy() override;
	Descriptor settings {};

private:
	std::shared_ptr<Prepared> mPrepared {};
};
template <> inline std::string GetType<BlackboardComponent>() {
	return "class IKIGAI::ECS::BlackboardComponent";
}
template <> inline std::string GetComponentName<BlackboardComponent>() {
	return "BlackboardComponent";
}
}
