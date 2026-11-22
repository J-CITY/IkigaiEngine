#pragma once

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::ECS {
class ParticleSimulationSystem {
public:
	static void update(SCENE_SYSTEM::Scene& scene, double dt);
};
}
