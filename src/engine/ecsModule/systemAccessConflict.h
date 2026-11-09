#pragma once
#include "utilsModule/idGenerator.h"
#include <cstddef>
#include <span>
#include <string_view>
#include <typeindex>
#include <unordered_set>
#include <vector>

namespace IKIGAI::ECS {

class ComponentManager;
using ComponentType = Id<ComponentManager>;

struct SystemScheduleView {
	std::string_view name;
	int priority;
	bool exclusiveBatch;
	const std::unordered_set<ComponentType>& reads;
	const std::unordered_set<ComponentType>& writes;
	const std::unordered_set<std::type_index>& resourceReads;
	const std::unordered_set<std::type_index>& resourceWrites;
	std::span<const size_t> predecessors;

	[[nodiscard]] bool declaresAccess() const noexcept {
		return !reads.empty() || !writes.empty() || !resourceReads.empty() || !resourceWrites.empty();
	}
};

[[nodiscard]] bool systemsAccessConflict(const SystemScheduleView& left, const SystemScheduleView& right) noexcept;

// Batch entries are indexes into `systems` (registration order).
[[nodiscard]] std::vector<std::vector<size_t>> buildSystemBatches(const std::vector<SystemScheduleView>& systems);

} // namespace IKIGAI::ECS
