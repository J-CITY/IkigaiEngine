#pragma once

#include "entityManager.h"

namespace IKIGAI::ECS {
class ComponentManager;
using ComponentType = Id<ComponentManager>;

// Runtime identity for one component instance. Resolving requires the owning manager.
template <class T> class ComponentHandle {
public:
	constexpr ComponentHandle() noexcept = default;
	constexpr bool operator==(const ComponentHandle&) const noexcept = default;

	// A non-empty handle can still be stale. Resolve it to check liveness.
	explicit constexpr operator bool() const noexcept {
		return mGeneration != 0;
	}

private:
	friend class ComponentManager;
	constexpr ComponentHandle(EntityHandle owner, ComponentType type, std::uint64_t generation) noexcept
		: mOwner(owner)
		, mType(type)
		, mGeneration(generation) { }

	EntityHandle mOwner {};
	ComponentType mType {};
	std::uint64_t mGeneration = 0;
};
}
