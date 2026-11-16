#pragma once

#include "../engine/ecsModule/systemManager.h"

namespace Example {

class MovementSystem : public IKIGAI::ECS::System {
public:
	MovementSystem();
	void onUpdate(IKIGAI::ECS::World& world, IKIGAI::ECS::CommandBuffer& commands, std::chrono::duration<double> dt) override;
};

void registerFollowerSystem(IKIGAI::ECS::World& world);

}
