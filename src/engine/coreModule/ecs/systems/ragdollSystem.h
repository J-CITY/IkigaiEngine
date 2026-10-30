#pragma once
#if USE_PHYSICS_3D
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class CharacterMotorComponent;
class RagdollSystem {
public:
	static void beforePhysics(SCENE_SYSTEM::Scene&, float dt);
	static void afterPhysics(SCENE_SYSTEM::Scene&);
	static void afterAnimation(SCENE_SYSTEM::Scene&, float dt);
	static void update(SCENE_SYSTEM::Scene&, float dt);
	static bool controlsMotor(SCENE_SYSTEM::Scene&, CharacterMotorComponent&);
};
}
#endif
