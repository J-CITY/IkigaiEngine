#pragma once
#include "component.h"
#include "coreModule/ecs/componentReference.h"
#include "scriptModule/contextPredicate.h"
#include "utilsModule/expressions/expression.h"
#include "utilsModule/variables/variableContext.h"
#include "variableDescriptor.h"
#include <optional>

namespace IKIGAI::ECS {
class VariableContextComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type {};
		std::string Schema {};
		std::map<std::string, VariableDescriptor> Variables {};
		template <class C> static auto serde(C& c, Descriptor& v) {
			using namespace serde::attribute;
			serde::serde_struct(c, v)
				.field(&Descriptor::Type, "VariableContextComponentType")
				.field(&Descriptor::Schema, "Schema", default_ { std::string {} })
				.field(&Descriptor::Variables, "Variables", default_ { std::map<std::string, VariableDescriptor> {} });
		}
	};
	explicit VariableContextComponent(UTILS::Ref<Object>);
	VariableContextComponent(UTILS::Ref<Object>, const ComponentBase::Descriptor&);
	Descriptor getDescriptor() const {
		return settings;
	}
	static auto GetMembers() {
		return std::tuple {};
	}
	size_t slot(const std::string&) const;
	DATA::ValueType type(size_t) const;
	const DATA::Value& read(size_t) const;
	const std::optional<DATA::Value>& readCurrent(size_t) const;
	void write(size_t, const DATA::Value&);
	void clear(size_t);
	void fire(size_t);
	uint64_t tick() const;
	void update(SCENE_SYSTEM::Scene&);
	void prepare(SCENE_SYSTEM::Scene&);
	std::shared_ptr<DATA::VariableContext> runtime() const;
	Descriptor settings {};

private:
	struct Input {
		size_t slot = 0;
		std::optional<PropertyBinding> property {};
		std::optional<DATA::EntityRef> entity {};
	};
	std::shared_ptr<DATA::VariableContext> mRuntime {};
	std::vector<Input> mInputs {};
};
template <> inline std::string GetType<VariableContextComponent>() {
	return "class IKIGAI::ECS::VariableContextComponent";
}
template <> inline std::string GetComponentName<VariableContextComponent>() {
	return "VariableContextComponent";
}

}
