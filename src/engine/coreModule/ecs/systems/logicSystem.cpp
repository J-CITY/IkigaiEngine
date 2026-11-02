#include "logicSystem.h"
#include <sceneModule/sceneManager.h>

IKIGAI::ECS::LogicSystem::LogicSystem() {
	// mComponentsRead.insert(typeid(LogicComponent).name());
	mName = "LogicSystem";
	mPriority = 10;
	mExclusiveBatch = true;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mReads.insert(cm->getComponentType<LogicComponent>());
}

void IKIGAI::ECS::LogicSystem::onAwake(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onAwake();
	}
}

void IKIGAI::ECS::LogicSystem::onStart(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onStart();
	}
}

void IKIGAI::ECS::LogicSystem::onEnable(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onEnable();
	}
}

void IKIGAI::ECS::LogicSystem::onDisable(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onDisable();
	}
}

void IKIGAI::ECS::LogicSystem::onDestroy(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onDestroy();
	}
}

void IKIGAI::ECS::LogicSystem::onUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onUpdate(dt);
	}
}

void IKIGAI::ECS::LogicSystem::onFixedUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onFixedUpdate(dt);
	}
}

void IKIGAI::ECS::LogicSystem::onLateUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::LogicComponent>()) {
		component._onLateUpdate(dt);
	}
}
