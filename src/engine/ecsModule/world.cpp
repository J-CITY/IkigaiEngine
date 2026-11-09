#include "world.h"

#include "commandBuffer.h"
#include "componentManager.h"

namespace IKIGAI::ECS {
World::World() {
	mComponentsManager = std::make_shared<ComponentManager>();
	mSystemsManager = std::make_unique<SystemManager>(*this);
}

World::~World() = default;

ComponentManager* World::getComponentManager() const noexcept {
	return mComponentsManager.get();
}

std::weak_ptr<ComponentManager> World::getComponentManagerWeak() const noexcept {
	return mComponentsManager;
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
		const EntityHandle handle = mComponentsManager->getEntityHandle(newEntity);
		commands->addCommand([manager = getComponentManagerWeak(), handle]() {
			if (const auto components = manager.lock()) {
				if (const auto entity = components->resolveEntity(handle)) {
					components->destroyEntity(*entity);
				}
			}
		});
		return;
	}
	mComponentsManager->destroyEntity(newEntity);
}

SystemManager* World::getSystemManager() const noexcept {
	return mSystemsManager.get();
}
}
