#pragma once
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::PHYSICS {
class PhysicWorld;
}
namespace IKIGAI::ECS::WaterSystem {
void advance(SCENE_SYSTEM::Scene& scene, float dt);
void present(SCENE_SYSTEM::Scene& scene);
#if USE_PHYSICS_3D
void stepPhysics(SCENE_SYSTEM::Scene& scene, PHYSICS::PhysicWorld& world, float dt);
#endif
}
