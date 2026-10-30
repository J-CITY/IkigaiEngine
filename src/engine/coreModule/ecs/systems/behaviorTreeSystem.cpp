#include "behaviorTreeSystem.h"
#include "coreModule/ecs/components/behaviorTreeComponent.h"
#include "sceneModule/scene.h"

namespace IKIGAI::ECS {
void BehaviorTreeSystem::applyCommands(SCENE_SYSTEM::Scene& scene) {
	for (const auto& object : scene.getObjects()) {
		if (auto component = object->getComponent<BehaviorTreeComponent>()) {
			component->applyCommands(scene);
		}
	}
}
void BehaviorTreeSystem::update(SCENE_SYSTEM::Scene& scene, double dt) {
	for (const auto& object : scene.getObjects()) {
		if (object->isAlive() && object->getIsActive()) {
			if (auto component = object->getComponent<BehaviorTreeComponent>()) {
				component->update(scene, dt);
			}
		}
	}
}
}
