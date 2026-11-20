#include "sceneObjectDescriptor.h"

#include "physics2dModule/features.h"
#include "physicsModule/features.h"
#include "resourceModule/jsonLoader.h"
#include "utilsModule/serialization/jsonSerde.h"
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
}

ECS::Object::Descriptor ReadSceneObjectDescriptor(nlohmann::json& data) {
	auto resolved = UTILS::ApplyPatches(data);
	if (resolved.isErr()) {
		throw std::runtime_error(resolved.unwrapErr().text);
	}
	data = resolved.unwrap();
	validateScriptFeatures(data);
	PHYSICS2D::validateFeatures(data);
	PHYSICS::validateFeatures(data);

	auto descriptor = DATA::DeserializeJson<ECS::Object::Descriptor>(data);
	if (descriptor.isErr()) {
		throw std::runtime_error(descriptor.unwrapErr().text);
	}
	return descriptor.unwrap();
}
}
