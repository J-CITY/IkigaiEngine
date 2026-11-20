#pragma once

#include "sceneModule/sceneSimulation.h"

namespace IKIGAI::DESTRUCTION {
class DestructionRuntime;
struct RuntimeSettings;
}

namespace IKIGAI::ECS {
std::unique_ptr<SCENE_SYSTEM::SceneSimulation> CreateSceneSimulation(SCENE_SYSTEM::PhysicsFactories physics);
#if USE_PHYSICS_3D
DESTRUCTION::RuntimeSettings& SceneDestructionSettings(SCENE_SYSTEM::Scene& scene);
std::shared_ptr<DESTRUCTION::DestructionRuntime>& SceneDestructionRuntime(SCENE_SYSTEM::Scene& scene);
#endif
}
