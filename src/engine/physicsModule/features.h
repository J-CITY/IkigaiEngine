#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace IKIGAI::PHYSICS {
inline void validateFeatures(const nlohmann::json& data) {
#if !USE_PHYSICS_3D
	if (data.is_object()) {
		for (const char* key :
			{ "Physics3DComponentType", "SoftBody3DComponentType", "Cloth3DComponentType", "Fluid3DComponentType", "ForceField3DComponentType",
			"PhysicsComponentType", "ThirdPersonControllerType", "CapsuleColliderComponentType", "CharacterMotorComponentType",
			"ThirdPersonCameraComponentType", "TraversalComponentType", "RagdollComponentType", "DestructibleMeshComponentType" }) {
			if (data.contains(key)) {
				throw std::runtime_error("Scene requires 3D physics, but USE_PHYSICS_3D=OFF");
			}
		}
	}
	if (data.is_structured()) {
		for (const auto& item : data) {
			validateFeatures(item);
		}
	}
#else
	(void)data;
#endif
}
}
