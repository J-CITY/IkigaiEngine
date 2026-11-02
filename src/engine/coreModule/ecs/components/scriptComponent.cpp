#if USE_LUA

#include "scriptComponent.h"
#include "../object.h"
#include <scriptModule/luaBinder.h>

using namespace IKIGAI;
using namespace IKIGAI::ECS;

EVENT::Event<UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent>> ScriptComponentEvents::destroyedEvent;
EVENT::Event<UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent>> ScriptComponentEvents::createdEvent;

ScriptComponent::ScriptComponent(UTILS::Ref<ECS::Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "ScriptComponent";
}

ScriptComponent::ScriptComponent(UTILS::Ref<ECS::Object> _obj, const std::string& _name)
	: name(_name)
	, ComponentBase(_obj) {
	__NAME__ = "ScriptComponent";
}

ScriptComponent::ScriptComponent(UTILS::Ref<ECS::Object> _obj, const Descriptor& _descriptor)
	: ComponentBase(_obj)
	, name(_descriptor.Path) {
	__NAME__ = "ScriptComponent";
}

ScriptComponent::~ScriptComponent() = default;

sol::table& ScriptComponent::getTable() {
	return object;
}

const std::string& ScriptComponent::getScriptName() const {
	return name;
}

void ScriptComponent::setScript(const std::string& _name) {
	name = _name;
	ScriptComponentEvents::createdEvent.run(getWeak<ScriptComponent>());
}

ScriptComponent::Descriptor ScriptComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<ScriptComponent>();
	descriptor.Path = getScriptName();
	return descriptor;
}
// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::ScriptComponent>("ScriptComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Path", &IKIGAI::ECS::ScriptComponent::getScriptName, &IKIGAI::ECS::ScriptComponent::setScript)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	);
// }


#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ScriptComponent>();
	return true;
}();
}

#endif
