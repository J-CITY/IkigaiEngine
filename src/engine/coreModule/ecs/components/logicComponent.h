#pragma once
#include <functional>
#include <string>

#include "component.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class LogicComponent : public ComponentBase {
	IKI_GENERATED_BODY(LogicComponent)
public:
	IKI_CLASS(Name = LogicComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(LogicComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "LogicComponentType"))
		std::string Type;
	};
	std::function<void()> _onAwake = [] { };
	std::function<void()> _onStart = [] { };
	std::function<void()> _onEnable = [] { };
	std::function<void()> _onDisable = [] { };
	std::function<void()> _onDestroy = [] { };
	std::function<void(std::chrono::duration<double>)> _onUpdate = [](std::chrono::duration<double>) { };
	std::function<void(std::chrono::duration<double>)> _onFixedUpdate = [](std::chrono::duration<double>) { };
	std::function<void(std::chrono::duration<double>)> _onLateUpdate = [](std::chrono::duration<double>) { };

public:
	LogicComponent(UTILS::Ref<ECS::Object> obj)
		: ComponentBase(obj) {
		__NAME__ = "LogicComponent";
	};
	LogicComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: LogicComponent(obj) { };

	virtual void onAwake() override {
		_onAwake();
	}
	virtual void onStart() override {
		_onStart();
	};
	virtual void onEnable() override {
		_onEnable();
	};
	virtual void onDisable() override {
		_onDisable();
	}
	virtual void onDestroy() override {
		_onDestroy();
	};
	virtual void onUpdate(std::chrono::duration<double> dt) override {
		_onUpdate(dt);
	};
	virtual void onFixedUpdate(std::chrono::duration<double> dt) override {
		_onFixedUpdate(dt);
	};
	virtual void onLateUpdate(std::chrono::duration<double> dt) override {
		_onLateUpdate(dt);
	};

	virtual void onDeserialize(nlohmann::json& j) override { }
	virtual void onSerialize(nlohmann::json& j) override { }

	[[nodiscard]] Descriptor getDescriptor() const;

public:
	static auto GetMembers() {
		return std::tuple { };
	}
};
template <> inline std::string ECS::GetType<LogicComponent>() {
	return "class IKIGAI::ECS::LogicComponent";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<LogicComponent>() {
	return "LogicComponent";
}
}

#include "generated/logicComponent.generated.h"
