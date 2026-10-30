#pragma once
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class BehaviorTreeSystem {
public:
	static void applyCommands(SCENE_SYSTEM::Scene&);
	static void update(SCENE_SYSTEM::Scene&, double dt);
};
}
