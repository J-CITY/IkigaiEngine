#pragma once

#include "componentHandle.h"
#include <string_view>
#include <typeindex>
#include <unordered_set>

namespace IKIGAI::ECS {

enum class AccessMode { Read, Write };

// Installed only while a checked system callback executes on its calling thread.
class SystemAccessScope {
public:
	SystemAccessScope(bool enabled, std::string_view system, const ComponentManager& manager, const std::unordered_set<ComponentType>& reads,
		const std::unordered_set<ComponentType>& writes, const std::unordered_set<std::type_index>& resourceReads,
		const std::unordered_set<std::type_index>& resourceWrites) noexcept;
	~SystemAccessScope();

	SystemAccessScope(const SystemAccessScope&) = delete;
	SystemAccessScope& operator=(const SystemAccessScope&) = delete;

	static void requireComponent(const ComponentManager& manager, ComponentType type, AccessMode mode);
	static void requireResource(std::type_index type, AccessMode mode);

private:
	static thread_local const SystemAccessScope* sCurrent;
	const SystemAccessScope* mPrevious = nullptr;
	std::string_view mSystem;
	const ComponentManager& mManager;
	const std::unordered_set<ComponentType>& mReads;
	const std::unordered_set<ComponentType>& mWrites;
	const std::unordered_set<std::type_index>& mResourceReads;
	const std::unordered_set<std::type_index>& mResourceWrites;
};
}
