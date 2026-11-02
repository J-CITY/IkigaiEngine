#pragma once
#include "entityManager.h"
#include "utilsModule/weakPtr.h"
#include <memory>

namespace IKIGAI::ECS {
class Component;
}

namespace IKIGAI::ECS {
class ComponentArrayInterface {
public:
	virtual ~ComponentArrayInterface() = default;
	virtual void entityDestroyed(Entity entity) = 0;
	// Move this entity's row into a same-typed destination array. Removes it from this array.
	virtual void transferEntityTo(Entity entity, ComponentArrayInterface& destination) = 0;
	virtual std::shared_ptr<ComponentArrayInterface> createEmptyFromThis() = 0;
	[[nodiscard]] virtual int getSize() const = 0;
	virtual Entity getEntity(size_t index) = 0;
	[[nodiscard]] virtual bool contains(Entity entity) const = 0;
	virtual UTILS::WeakPtr<ECS::Component> getComponentBasePtr(Entity entity) = 0;
};
}
