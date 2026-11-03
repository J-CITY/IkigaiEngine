#pragma once

#include "ecsModule/systemManager.h"

namespace IKIGAI::ECS {

class PropertyAnimationSystem : public ECS::System {
public:
	PropertyAnimationSystem();
	void onUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) override;
};

} // namespace IKIGAI::ECS
