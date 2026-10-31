#pragma once
#if USE_PHYSICS_3D
#include "mathModule/math.h"
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class DestructionSystem {
public:
	static void beforePhysics(SCENE_SYSTEM::Scene& scene, float dt);
	static void afterPhysics(SCENE_SYSTEM::Scene& scene, float dt);
	static void beginFrame(SCENE_SYSTEM::Scene& scene);
	static void present(SCENE_SYSTEM::Scene& scene, float alpha);
	static void radialDamage(SCENE_SYSTEM::Scene& scene, MATH::Vector3f center, float radius, float damage, MATH::Vector3f cuttingNormal);
};
}
#endif
