#pragma once

#include "virtualGeometryModule/runtime/virtualGeometryInstance.h"

#include <cstdint>
#include <vector>

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::RENDER {
// Consume the result before the scene can change its components or material slots.
std::vector<VG::VirtualGeometryInstance> CollectVirtualGeometryInstances(SCENE_SYSTEM::Scene& scene, uint64_t frame, bool collectShadows);
}
