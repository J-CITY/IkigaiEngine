#include "sceneObjectDescriptor.h"

#include "resourceModule/jsonLoader.h"
#include "utilsModule/serialization/jsonSerde.h"
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

ECS::Object::Descriptor ReadSceneObjectDescriptor(nlohmann::json& data) {
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

	auto descriptor = DATA::DeserializeJson<ECS::Object::Descriptor>(data);
	if (descriptor.isErr()) {
		throw std::runtime_error(descriptor.unwrapErr().text);
	}
	return descriptor.unwrap();
}
}
