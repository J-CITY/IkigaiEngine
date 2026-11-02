#pragma once
#include "utilsModule/idGenerator.h"
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <unordered_set>
#include <vector>

namespace IKIGAI::ECS {

class ComponentManager;
using ComponentType = Id<ComponentManager>;

[[nodiscard]] inline bool systemsAccessConflict(const std::unordered_set<ComponentType>& readsA, const std::unordered_set<ComponentType>& writesA,
	const std::unordered_set<ComponentType>& readsB, const std::unordered_set<ComponentType>& writesB) noexcept {
	if (readsA.empty() && writesA.empty()) {
		return true;
	}
	if (readsB.empty() && writesB.empty()) {
		return true;
	}

	for (const auto id : writesA) {
		if (readsB.contains(id) || writesB.contains(id)) {
			return true;
		}
	}

	for (const auto id : readsA) {
		if (writesB.contains(id)) {
			return true;
		}
	}

	return false;
}

// Lower priority runs first. Equal priorities keep registration order.
struct SystemScheduleView {
	int priority = 0;
	bool exclusiveBatch = false;
	const std::unordered_set<ComponentType>* reads = nullptr;
	const std::unordered_set<ComponentType>* writes = nullptr;
};

// Batch entries are indexes into `systems` (registration order).
[[nodiscard]] inline std::vector<std::vector<size_t>> buildSystemBatches(const std::vector<SystemScheduleView>& systems) {
	std::vector<size_t> order(systems.size());
	std::iota(order.begin(), order.end(), size_t { 0 });
	std::stable_sort(order.begin(), order.end(), [&systems](size_t left, size_t right) { return systems[left].priority < systems[right].priority; });

	std::vector<std::vector<size_t>> batches;
	for (const size_t index : order) {
		const SystemScheduleView& system = systems[index];
		bool added = false;
		if (!system.exclusiveBatch && !batches.empty()) {
			std::vector<size_t>& batch = batches.back();
			const bool batchIsExclusive = systems[batch.front()].exclusiveBatch;
			if (!batchIsExclusive) {
				bool conflict = false;
				for (const size_t existing : batch) {
					if (systemsAccessConflict(*system.reads, *system.writes, *systems[existing].reads, *systems[existing].writes)) {
						conflict = true;
						break;
					}
				}
				if (!conflict) {
					batch.push_back(index);
					added = true;
				}
			}
		}
		if (!added) {
			batches.push_back({ index });
		}
	}
	return batches;
}

} // namespace IKIGAI::ECS
