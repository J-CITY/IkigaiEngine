#include "propertyAnimationSystem.h"

#include "coreModule/ecs/components/propertyAnimationComponent.h"
#include "resourceModule/serviceManager.h"

using namespace IKIGAI::ECS;

PropertyAnimationSystem::PropertyAnimationSystem() {
	mName = "PropertyAnimationSystem";
	mPriority = 15;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mReads.insert(cm->getComponentType<PropertyAnimationComponent>());
}

void PropertyAnimationSystem::onUpdate(ECS::World& world, ECS::CommandBuffer&, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<PropertyAnimationComponent>()) {
		if (auto* animation = component.getRuntimeAnimation()) {
			animation->update(static_cast<float>(dt.count()));
		}
	}
}
