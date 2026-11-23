#pragma once

#include "ecsModule/systemManager.h"

namespace IKIGAI::ECS {

class GuiFieldSystem : public System {
public:
	GuiFieldSystem();
	void onUpdate(World& world, CommandBuffer& commands, std::chrono::duration<double> delta) override;
	void onDestroy(World& world) override;
};

}
