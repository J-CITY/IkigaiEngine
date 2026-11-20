#include "componentEditorCatalog.h"

#ifdef USE_EDITOR
#include "componentEditorHooks.h"
#include "coreModule/ecs/componentCatalog.h"

namespace IKIGAI::EDITOR {
namespace {
	const detail::ComponentEditorHooks* findHooks(std::type_index type) {
		static const auto registry = [] {
			detail::ComponentEditorRegistry hooks;
			detail::registerGameplayComponentEditors(hooks);
			detail::registerGuiComponentEditors(hooks);
			detail::registerPhysicsComponentEditors(hooks);
			detail::registerRenderComponentEditors(hooks);
			return hooks;
		}();
		const auto it = registry.find(type);
		return it == registry.end() ? nullptr : &it->second;
	}
}

void inspectComponent(UTILS::WeakPtr<ECS::ComponentBase> component) {
	if (component) {
		if (const auto* hooks = findHooks(typeid(*component))) {
			hooks->inspect(component);
		}
	}
}

void collectTimelineProperties(
	const std::string& name, ECS::Object::Id id, UTILS::WeakPtr<ECS::ComponentBase> component, TimelineProperties& properties) {
	if (component) {
		if (const auto* hooks = findHooks(typeid(*component))) {
			hooks->collectTimeline(name, id, component, properties);
		}
	}
}

void bindTimelineProperty(
	const std::string& name, ECS::Object::Id id, const std::string& componentName, const std::string& propertyName, TimelineProperty& property) {
	if (const auto* component = ECS::ComponentCatalog::instance().findByName(componentName)) {
		if (const auto* hooks = findHooks(component->type)) {
			hooks->bindTimeline(name, id, componentName, propertyName, property);
		}
	}
}
}
#endif
