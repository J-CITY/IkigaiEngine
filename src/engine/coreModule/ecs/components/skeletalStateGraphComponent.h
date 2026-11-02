#pragma once
#include "component.h"
#include "coreModule/ecs/componentReference.h"
#include "skeletalModule/skeletalStateGraph.h"

namespace IKIGAI::ECS {
class VariableContextComponent;
class SkeletalStateGraphComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		std::string Graph;
		ComponentReference Context;
		ComponentReference Animator;
		std::map<std::string, std::string> Bindings;
		std::string PoseOffset;
		ComponentReference RootMotionMotor;
		bool ApplyRootMotion = false;
		template <class C> static auto serde(C& c, Descriptor& v) {
			serde::serde_struct(c, v)
				.field(&Descriptor::Type, "SkeletalStateGraphComponentType")
				.field(&Descriptor::Graph, "Graph")
				.field(&Descriptor::Context, "Context")
				.field(&Descriptor::Animator, "Animator")
				.field(&Descriptor::Bindings, "Bindings")
				.field(&Descriptor::PoseOffset, "PoseOffset", serde::attribute::default_ { std::string {} })
				.field(&Descriptor::RootMotionMotor, "RootMotionMotor", serde::attribute::default_ { ComponentReference {} })
				.field(&Descriptor::ApplyRootMotion, "ApplyRootMotion", serde::attribute::default_ { false });
		}
	};
	explicit SkeletalStateGraphComponent(UTILS::Ref<Object>);
	SkeletalStateGraphComponent(UTILS::Ref<Object>, const ComponentBase::Descriptor&);
	void onDisable() override;
	void onDestroy() override;
	Descriptor getDescriptor() const {
		return settings;
	}
	static auto GetMembers() {
		return std::tuple {};
	}
	void update(SCENE_SYSTEM::Scene&, float);
	std::shared_ptr<SKELETON::SkeletalStateGraphInstance> instance;
	Descriptor settings;
	bool paused = false;

private:
	struct Binding {
		size_t context = 0;
		size_t parameter = 0;
		DATA::ValueType type;
	};
	std::vector<Binding> bindings;
	std::shared_ptr<UTILS::Environment> environment;
	unsigned revision = 0;
	uint64_t lastTick = 0;
	UTILS::WeakPtr<ComponentBase> boundContext;
	UTILS::WeakPtr<ComponentBase> playbackTarget;
};
template <> inline std::string GetType<SkeletalStateGraphComponent>() {
	return "class IKIGAI::ECS::SkeletalStateGraphComponent";
}
template <> inline std::string GetComponentName<SkeletalStateGraphComponent>() {
	return "SkeletalStateGraphComponent";
}

}
