#include "componentManager.h"

namespace IKIGAI::ECS {
ComponentArrayInterface* ComponentManager::findArray(Entity entity, ComponentType type, std::uint64_t entityGeneration) const {
	const auto record = mEntityRecords.find(entity);
	if (record == mEntityRecords.end() || (entityGeneration != 0 && record->second.generation != entityGeneration)) {
		return nullptr;
	}
	const auto& columns = record->second.archetype->mComponentArrays;
	if (const auto column = columns.find(type); column != columns.end()) {
		return column->second.get();
	}
	const auto sparse = mSparseArrays.find(type);
	return sparse == mSparseArrays.end() ? nullptr : sparse->second.get();
}

thread_local const SystemAccessScope* SystemAccessScope::sCurrent = nullptr;

SystemAccessScope::SystemAccessScope(bool enabled, std::string_view system, const ComponentManager& manager,
	const std::unordered_set<ComponentType>& reads, const std::unordered_set<ComponentType>& writes,
	const std::unordered_set<std::type_index>& resourceReads, const std::unordered_set<std::type_index>& resourceWrites) noexcept
	: mPrevious(sCurrent)
	, mSystem(system)
	, mManager(manager)
	, mReads(reads)
	, mWrites(writes)
	, mResourceReads(resourceReads)
	, mResourceWrites(resourceWrites) {
	if (enabled) {
		sCurrent = this;
	}
}

SystemAccessScope::~SystemAccessScope() {
	sCurrent = mPrevious;
}

void SystemAccessScope::requireComponent(const ComponentManager& manager, ComponentType type, AccessMode mode) {
	if (const auto* scope = sCurrent) {
		if (&scope->mManager != &manager) {
			throw std::logic_error("ECS system " + std::string(scope->mSystem) + " accessed components in another world");
		}
		if (!scope->mWrites.contains(type) && (mode == AccessMode::Write || !scope->mReads.contains(type))) {
			throw std::logic_error(
				"ECS system " + std::string(scope->mSystem) + " has undeclared component access for type " + std::to_string(type.value()));
		}
	}
}

void SystemAccessScope::requireResource(std::type_index type, AccessMode mode) {
	if (const auto* scope = sCurrent) {
		if (!scope->mResourceWrites.contains(type) && (mode == AccessMode::Write || !scope->mResourceReads.contains(type))) {
			throw std::logic_error("ECS system " + std::string(scope->mSystem) + " has undeclared resource access for type " + type.name());
		}
	}
}
}
