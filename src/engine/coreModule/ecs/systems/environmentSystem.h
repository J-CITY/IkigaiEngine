#pragma once
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class EnvironmentComponent;
class EnvironmentSystem {
public:
	static EnvironmentComponent* active(SCENE_SYSTEM::Scene& scene);
	static void update(SCENE_SYSTEM::Scene& scene, double dt);
};
}
