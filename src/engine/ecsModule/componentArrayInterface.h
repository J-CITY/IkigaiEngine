#pragma once
#include "entityManager.h"
#include "weakPtr.h"
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace IKIGAI::ECS {
class Component;
}

namespace IKIGAI::ECS {
struct ArchetypeRows {
	static constexpr auto missing = std::numeric_limits<std::size_t>::max();
	std::vector<Entity> entities;
	std::function<std::size_t(Entity)> find;
};

struct IndexedRowMove {
	std::uint32_t source = 0;
	std::uint32_t destination = 0;
};

class ComponentArrayInterface {
public:
	virtual ~ComponentArrayInterface() = default;
	virtual void entityDestroyed(Entity entity) = 0;
	// Move this entity's row into a same-typed destination array. Removes it from this array.
	virtual void transferEntityTo(Entity entity, ComponentArrayInterface& destination) = 0;
	virtual std::shared_ptr<ComponentArrayInterface> createEmptyFromThis() = 0;
	[[nodiscard]] virtual int getSize() const = 0;
	virtual Entity getEntity(size_t index) = 0;
	[[nodiscard]] virtual std::span<const Entity> getEntities() const noexcept = 0;
	[[nodiscard]] virtual bool contains(Entity entity) const = 0;
	virtual const Component* tryGetBaseData(Entity entity, std::uint64_t generation) const = 0;
	virtual UTILS::WeakPtr<ECS::Component> getComponentBasePtr(Entity entity) = 0;

	virtual void attachRows(const std::shared_ptr<const ArchetypeRows>& rows) = 0;
	[[nodiscard]] virtual bool hasTrivialLifecycle() const noexcept = 0;
	virtual std::size_t reserveIndexedRows(std::size_t required) = 0;
	virtual void appendIndexedMove(std::size_t sourceRow, ComponentArrayInterface& destination) noexcept = 0;
	virtual void transferIndexedRange(std::size_t first, std::size_t count, ComponentArrayInterface& destination) noexcept = 0;
	virtual void appendIndexedRows(std::span<const IndexedRowMove> outgoing, ComponentArrayInterface& destination, bool destinationOrder) noexcept
		= 0;
	virtual void compactIndexedRows(std::size_t retained, std::span<const IndexedRowMove> compacted) noexcept = 0;
	virtual void eraseIndexedRow(std::size_t row) noexcept = 0;
};
}
