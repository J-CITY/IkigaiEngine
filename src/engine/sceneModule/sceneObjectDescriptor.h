#pragma once

#include "coreModule/ecs/object.h"

namespace IKIGAI::SCENE_SYSTEM {
ECS::Object::Descriptor ReadSceneObjectDescriptor(nlohmann::json& data);
}
