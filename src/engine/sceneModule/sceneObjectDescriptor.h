#pragma once

#include "coreModule/ecs/object.h"
#include "utilsModule/serialization/jsonValueAdaptor.h"

namespace IKIGAI::SCENE_SYSTEM {
struct SceneObjectDocument : ECS::Object::Descriptor {
	std::vector<nlohmann::json> Components;

	template <class Context> static void serde(Context& context, SceneObjectDocument& value) {
		using Self = SceneObjectDocument;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::Name, "Name")
			.field(&Self::Tag, "Tag", default_ { "" })
			.field(&Self::Id, "Id")
			.field(&Self::ParentId, "ParentId", default_ { -1 })
			.field(&Self::IsActive, "IsActive", default_ { true })
			.field(&Self::IsVisible, "IsVisible", default_ { true })
			.field(&Self::Components, "Components");
	}
};

struct SceneComponentDescriptor {
	std::type_index type;
	std::shared_ptr<const ECS::ComponentBase::Descriptor> value;
};

struct SceneObjectDescriptor : ECS::Object::Descriptor {
	std::vector<SceneComponentDescriptor> Components;
};

SceneObjectDescriptor ParseSceneObject(const SceneObjectDocument& document);
SceneObjectDescriptor ReadSceneObjectDescriptor(nlohmann::json& data);
std::vector<SceneObjectDescriptor> ReadSceneObjectDescriptors(const nlohmann::json& objects);
void AddSceneComponent(ECS::Object& object, const SceneComponentDescriptor& component);
nlohmann::json WriteSceneObject(const ECS::Object& object);
}
