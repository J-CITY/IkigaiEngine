#pragma once

#include "ecsModule/systemManager.h"

namespace IKIGAI::ECS {
class GuiAnimationSystem : public System {
public:
	GuiAnimationSystem();
	void onUpdate(World& world, CommandBuffer& commands, std::chrono::duration<double> dt) override;
};
}
