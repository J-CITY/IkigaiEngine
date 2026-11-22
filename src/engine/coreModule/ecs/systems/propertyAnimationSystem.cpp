#include "propertyAnimationSystem.h"

#include "coreModule/ecs/components/propertyAnimationComponent.h"
#include "ecsModule/world.h"
#include "resourceModule/serviceManager.h"

using namespace IKIGAI::ECS;

PropertyAnimationSystem::PropertyAnimationSystem() {
	mName = "PropertyAnimationSystem";
	setPhases({ SystemPhase::Update });
	mPriority = 15;
	// Animation bindings can invoke setters on arbitrary components.
	mExclusiveBatch = true;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mWrites.insert(cm->getComponentType<PropertyAnimationComponent>());
}

void PropertyAnimationSystem::onUpdate(ECS::World& world, ECS::CommandBuffer&, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<PropertyAnimationComponent>()) {
		if (auto* animation = component.getRuntimeAnimation()) {
			animation->update(static_cast<float>(dt.count()));
		}
	}
}
