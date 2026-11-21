#include "sceneObjectDescriptor.h"

#include "coreModule/ecs/componentCatalog.h"
#include "resourceModule/jsonLoader.h"
#include "sceneDocument.h"
#include "utilsModule/serialization/jsonSerde.h"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace IKIGAI::SCENE_SYSTEM {
namespace {
#if (defined(USE_LUA) && !USE_LUA) || (defined(USE_IKIGAI_SCRIPT) && !USE_IKIGAI_SCRIPT)
	void validateScriptFeatures(const nlohmann::json& data) {
		if (data.is_object()) {
#if defined(USE_LUA) && !USE_LUA
			if (data.contains("ScriptComponentType")) {
				throw std::runtime_error("Scene requires Lua ScriptComponent, but USE_LUA=OFF");
			}
#endif
#if defined(USE_IKIGAI_SCRIPT) && !USE_IKIGAI_SCRIPT
			if (data.contains("IkigaiScriptComponentType")) {
				throw std::runtime_error("Scene requires IkigaiScriptComponent, but USE_IKIGAI_SCRIPT=OFF");
			}
#endif
		}
		if (data.is_structured()) {
			for (const auto& value : data) {
				validateScriptFeatures(value);
			}
		}
	}
#else
	void validateScriptFeatures(const nlohmann::json&) { }
#endif

#if !USE_PHYSICS_2D || !USE_PHYSICS_3D
	void validatePhysicsFeatures(const nlohmann::json& data, std::initializer_list<const char*> keys, const char* error) {
		if (data.is_object()) {
			for (const auto* key : keys) {
				if (data.contains(key)) {
					throw std::runtime_error(error);
				}
			}
		}
		if (data.is_structured()) {
			for (const auto& value : data) {
				validatePhysicsFeatures(value, keys, error);
			}
		}
	}
#endif
}

SceneObjectDescriptor ParseSceneObject(const SceneObjectDocument& document) {
	SceneObjectDescriptor descriptor { static_cast<const ECS::Object::Descriptor&>(document), {} };
	descriptor.Components.reserve(document.Components.size());
	const auto& catalog = ECS::ComponentCatalog::instance();
	for (const auto& component : document.Components) {
		const auto* record = catalog.findInJson(component);
		if (!record) {
			const auto key = DOCUMENT::ComponentTypeKey(component);
			throw std::runtime_error("Unsupported scene component: " + key + " = " + component.at(key).get<std::string>()
				+ ". Check that the component is registered and its feature is enabled in this build.");
		}
		descriptor.Components.push_back({ record->type, record->read(component) });
	}
	return descriptor;
}

SceneObjectDescriptor ReadSceneObjectDescriptor(nlohmann::json& data) {
	auto resolved = UTILS::ApplyPatches(data);
	if (resolved.isErr()) {
		throw std::runtime_error(resolved.unwrapErr().text);
	}
	data = resolved.unwrap();
	validateScriptFeatures(data);
#if !USE_PHYSICS_2D
	validatePhysicsFeatures(data,
		{ "Physics2DComponentType", "SoftBody2DComponentType", "Fluid2DComponentType", "Joint2DComponentType", "ForceField2DComponentType",
		"Cloth2DComponentType" },
		"Scene requires 2D physics, but USE_PHYSICS_2D=OFF");
#endif
#if !USE_PHYSICS_3D
	validatePhysicsFeatures(data,
		{ "Physics3DComponentType", "SoftBody3DComponentType", "Cloth3DComponentType", "Fluid3DComponentType", "ForceField3DComponentType",
		"PhysicsComponentType", "ThirdPersonControllerType", "CapsuleColliderComponentType", "CharacterMotorComponentType",
		"ThirdPersonCameraComponentType", "TraversalComponentType", "RagdollComponentType", "DestructibleMeshComponentType" },
		"Scene requires 3D physics, but USE_PHYSICS_3D=OFF");
#endif

	auto descriptor = DATA::DeserializeJson<SceneObjectDocument>(data);
	if (descriptor.isErr()) {
		throw std::runtime_error(descriptor.unwrapErr().text);
	}
	return ParseSceneObject(descriptor.unwrap());
}

std::vector<SceneObjectDescriptor> ReadSceneObjectDescriptors(const nlohmann::json& objects) {
	std::vector<SceneObjectDescriptor> descriptors;
	descriptors.reserve(objects.size());
	for (auto object : objects) {
		descriptors.push_back(ReadSceneObjectDescriptor(object));
	}
	return descriptors;
}

void AddSceneComponent(ECS::Object& object, const SceneComponentDescriptor& component) {
	ECS::ComponentCatalog::instance().findByType(component.type)->apply(object, *component.value);
}

nlohmann::json WriteSceneObject(const ECS::Object& object) {
	SceneObjectDocument document { object.getDescriptor(), {} };
	std::vector<std::pair<std::string_view, nlohmann::json>> components;
	const auto& catalog = ECS::ComponentCatalog::instance();
	for (const auto& [type, component] : object.getComponents()) {
		if (const auto* record = catalog.findByType(type); component && record) {
			components.emplace_back(record->name, record->write(*component));
		}
	}
	std::sort(components.begin(), components.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
	for (auto& component : components) {
		document.Components.push_back(std::move(component.second));
	}
	auto json = UTILS::ToJson(document);
	if (json.isErr()) {
		throw std::runtime_error(json.unwrapErr().text);
	}
	return json.unwrap();
}
}
