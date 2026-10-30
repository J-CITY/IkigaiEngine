#pragma once
#if USE_PHYSICS_3D
#include "ecsModule/systemManager.h"
#include "physicsModule/physicWorld.h"
#include <memory>
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::ECS {
class Physics3DSystem final : public ECS::System {
public:
	explicit Physics3DSystem(SCENE_SYSTEM::Scene& scene, std::shared_ptr<PHYSICS::World> simulation = std::make_shared<PHYSICS::World>());
	void onFixedUpdate(ECS::World&, ECS::CommandBuffer&, std::chrono::duration<double>) override;
	PHYSICS::World& simulation() {
		return *mSimulation;
	}
	std::shared_ptr<PHYSICS::World> sharedSimulation() const {
		return mSimulation;
	}
	bool paused = false, singleStep = false;
#ifdef USE_EDITOR
	void drawDebug();
#endif
private:
	SCENE_SYSTEM::Scene& mScene;
	std::shared_ptr<PHYSICS::World> mSimulation;
};

}

#endif // USE_PHYSICS_3D
