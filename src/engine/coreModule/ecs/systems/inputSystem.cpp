#include "inputSystem.h"
#include "../components/inputComponent.h"
#include <sceneModule/sceneManager.h>
#include "resourceModule/serviceManager.h"
#include "windowModule/inputManager/inputActions.h"

IKIGAI::ECS::InputSystem::InputSystem() {
	//mComponentsRead.insert(typeid(InputComponent).name());
	mName = "InputSystem";
	auto cm = RESOURCES::ServiceManager::Get<ECS2::World>().getComponentManager();
	mReads.insert(cm->getComponentType<InputComponent>());
}

void IKIGAI::ECS::InputSystem::onUpdate(ECS2::World& world, ECS2::CommandBuffer& cb, std::chrono::duration<double> dt) {
	//world.getComponentManager()->forEach<ECS::InputComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::InputComponent& component) {
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
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::InputComponent>()) {
	//	if (component.getActive()) {
	//		component.getEventFunc()(dt);
	//	}
	//}
}

