#pragma once
#include <memory>

#include "entityManager.h"
#include "utilsModule/idGenerator.h"
#include <string>

namespace IKIGAI::ECS {
class SystemManager;
class ComponentManager;
template <typename... Components> class SystemBuilder;

class World : public IdGenerator<World> {
public:
	World();
	~World();

	[[nodiscard]] ComponentManager* getComponentManager() const noexcept;
	[[nodiscard]] std::weak_ptr<ComponentManager> getComponentManagerWeak() const noexcept;
	[[nodiscard]] SystemManager* getSystemManager() const noexcept;

	template <typename... T> SystemBuilder<T...> system(std::string name);

	Entity createEntity() const;
	void registerEntity(Entity newEntity) const;
	void destroyEntity(Entity newEntity) const;

private:
	std::unique_ptr<SystemManager> mSystemsManager;
	std::shared_ptr<ComponentManager> mComponentsManager;
};
}

#include "systemBuilder.h"
#include "systemManager.h"

namespace IKIGAI::ECS {
template <typename... T> SystemBuilder<T...> World::system(std::string name) {
	return SystemBuilder<T...>(*getSystemManager(), name);
}
}
