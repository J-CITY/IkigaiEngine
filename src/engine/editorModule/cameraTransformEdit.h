#pragma once

#if USE_JSON_VIEWS && !defined(OCULUS) && defined(USE_EDITOR)
#include "mathModule/math.h"
#include <memory>

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::EDITOR {
bool IsCameraTransformLocked(const ECS::Object& object);
void RecordCameraTransform(
	const std::shared_ptr<ECS::Object>& object, std::weak_ptr<void> sceneLifetime, MATH::Vector3f position, MATH::QuaternionF rotation);
}
#endif
