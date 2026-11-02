#pragma once

#include "ecsModule/systemManager.h"

namespace IKIGAI::ECS {
class GuiLayoutSystem : public System {
public:
	GuiLayoutSystem();
	void onUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override;
};
}
