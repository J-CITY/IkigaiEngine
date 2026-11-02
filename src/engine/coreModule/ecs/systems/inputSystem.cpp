#include "inputSystem.h"
#include "../components/inputComponent.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/inputManager/inputActions.h"
#include <sceneModule/sceneManager.h>

IKIGAI::ECS::InputSystem::InputSystem() {
	// mComponentsRead.insert(typeid(InputComponent).name());
	mName = "InputSystem";
	mPriority = 20;
	mExclusiveBatch = true;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mReads.insert(cm->getComponentType<InputComponent>());
}

void IKIGAI::ECS::InputSystem::onUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	// world.getComponentManager()->forEach<ECS::InputComponent>(
	//	[&](IKIGAI::ECS::Entity e, ECS::InputComponent& component) {
	//		if (component.getActive()) {
	//			component.getEventFunc()(dt);
	//		}
	//	}
	//);
	INPUT_SYSTEM::InputActions* actions = nullptr;
	if (RESOURCES::ServiceManager::Check<INPUT_SYSTEM::InputActions>()) {
		actions = &RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputActions>();
	}
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::InputComponent>()) {
		if (!component.getActive()) {
			continue;
		}
		component.getEventFunc()(dt);
		if (actions && component.getActionCallback()) {
			for (const auto& actionName : actions->getActionNames(component.getMapName())) {
				for (auto phase : actions->getPhasesThisFrame(actionName)) {
					component.getActionCallback()(actionName, phase);
				}
			}
		}
	}
}
