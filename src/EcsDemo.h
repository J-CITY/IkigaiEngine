#pragma once

#include "ecsDemo/components.h"
#include "ecsDemo/systems.h"

namespace IKIGAI::ECS {
class Object;
}

namespace Example {

// Register extensions before loading trees.
void RegisterBehaviorTreeTaskExample();
void ConfigureBehaviorTree(IKIGAI::ECS::Object& npc, int candidateObjectId, bool controlMotor = true);

class EcsDemo {
public:
	static void run();
};

}
