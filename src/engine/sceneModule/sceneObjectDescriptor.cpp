#include "sceneObjectDescriptor.h"

#include "physics2dModule/features.h"
#include "physicsModule/features.h"
#include "resourceModule/jsonLoader.h"
#include "scriptModule/scriptFeatures.h"
#include "utilsModule/serialization/jsonSerde.h"
#include <stdexcept>

namespace IKIGAI::SCENE_SYSTEM {
ECS::Object::Descriptor ReadSceneObjectDescriptor(nlohmann::json& data) {
	auto resolved = UTILS::ApplyPatches(data);
	if (resolved.isErr()) {
		throw std::runtime_error(resolved.unwrapErr().text);
	}
	data = resolved.unwrap();
	SCRIPTING::ValidateScriptFeatures(data);
	PHYSICS2D::validateFeatures(data);
	PHYSICS::validateFeatures(data);

	auto descriptor = DATA::DeserializeJson<ECS::Object::Descriptor>(data);
	if (descriptor.isErr()) {
		throw std::runtime_error(descriptor.unwrapErr().text);
	}
	return descriptor.unwrap();
}
}
