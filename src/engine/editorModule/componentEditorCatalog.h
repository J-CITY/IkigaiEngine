#pragma once

#ifdef USE_EDITOR

#include "coreModule/ecs/object.h"
#include "utilsModule/animation.h"

#include <map>
#include <string>

namespace IKIGAI::EDITOR {

struct TimelineProperty {
	TimelineProperty() = default;
	TimelineProperty(std::string objName, IKIGAI::ECS::Object::Id objId, std::string componentName, std::string propertyName,
		IKIGAI::ANIMATION::AnimationProperty animProp, IKIGAI::ANIMATION::PropType defaultValue)
		: objName(objName)
		, objId(objId)
		, componentName(componentName)
		, propertyName(propertyName)
		, animProp(animProp)
		, defaultValue(defaultValue) { }

	std::string objName;
	IKIGAI::ECS::Object::Id objId { IKIGAI::ECS::Entity::ID(0) };
	std::string componentName;
	std::string propertyName;
	IKIGAI::ANIMATION::AnimationProperty animProp;
	IKIGAI::ANIMATION::PropType defaultValue;
};

using TimelineProperties = std::map<std::string, TimelineProperty>;

void inspectComponent(UTILS::WeakPtr<ECS::ComponentBase> component);
void collectTimelineProperties(
	const std::string& name, ECS::Object::Id id, UTILS::WeakPtr<ECS::ComponentBase> component, TimelineProperties& properties);
void bindTimelineProperty(
	const std::string& name, ECS::Object::Id id, const std::string& componentName, const std::string& propertyName, TimelineProperty& property);
}
#endif
