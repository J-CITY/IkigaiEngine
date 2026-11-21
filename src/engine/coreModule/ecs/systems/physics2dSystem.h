#pragma once
#if USE_PHYSICS_2D
#include "ecsModule/systemManager.h"
#include "physics2dModule/physics2d.h"
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
// Scene-owned ECS adapter. App calls this in a dedicated phase AFTER fixed
// gameplay callbacks, rather than relying on unordered ECS batch iteration.
class Physics2DSystem final : public ECS::System {
public:
	explicit Physics2DSystem(SCENE_SYSTEM::Scene& scene, std::shared_ptr<PHYSICS2D::World> simulation = std::make_shared<PHYSICS2D::World>());
	void onFixedUpdate(ECS::World&, ECS::CommandBuffer&, std::chrono::duration<double> dt) override;
	PHYSICS2D::World& simulation() {
		return *mSimulation;
	}
	bool paused = false;
	bool singleStep = false;

private:
	SCENE_SYSTEM::Scene& mScene;
	std::shared_ptr<PHYSICS2D::World> mSimulation;
};

}
#endif
