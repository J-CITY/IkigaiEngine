#include "world.h"

#include "commandBuffer.h"
#include "componentManager.h"

namespace IKIGAI::ECS {
World::World() {
	mComponentsManager = std::make_unique<ComponentManager>();
	mSystemsManager = std::make_unique<SystemManager>(*this);
}

World::~World() = default;

ComponentManager* World::getComponentManager() const noexcept {
	return mComponentsManager.get();
}

Entity World::createEntity() const {
	Entity newEntity;
	mComponentsManager->registerEntity(newEntity);
	return newEntity;
}

void World::registerEntity(Entity newEntity) const {
	mComponentsManager->registerEntity(newEntity);
}

void World::destroyEntity(Entity newEntity) const {
	if (CommandBuffer* commands = activeCommandBuffer()) {
		ComponentManager* components = mComponentsManager.get();
		commands->addCommand([components, newEntity]() { components->destroyEntity(newEntity); });
		return;
	}
	mComponentsManager->destroyEntity(newEntity);
}

SystemManager* World::getSystemManager() const noexcept {
	return mSystemsManager.get();
}
}
