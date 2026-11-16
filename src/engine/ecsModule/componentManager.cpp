#include "componentManager.h"

namespace IKIGAI::ECS {
ComponentManager::ComponentManager()
	: ComponentManager(ArchetypeLayout::PerColumn) { }

ComponentManager::ComponentManager(ArchetypeLayout layout)
	: mIndexedRecords(layout == ArchetypeLayout::SharedRows ? std::make_shared<EntityRecords>() : nullptr)
	, mEntityRecords(mIndexedRecords ? *mIndexedRecords : mLocalRecords) { }

void ComponentManager::prepareIndexedTransition(const Record& record, Archetype& to) {
	if (record.archetype.get() == &to || to.mComponentArrays.empty()) {
		return;
	}
	auto& entities = to.mRows->entities;
	if (entities.size() == entities.capacity()) {
		entities.reserve(entities.size() + entities.size() / 2 + 1);
	}
	for (const auto& [type, column] : to.mComponentArrays) {
		column->reserveIndexedRow();
	}
}

void ComponentManager::commitIndexedTransition(
	Entity entity, Record& record, const std::shared_ptr<Archetype>& to, std::span<const ComponentType> replaced) noexcept {
	auto& from = *record.archetype;
	if (&from == to.get()) {
		return;
	}
	const auto sourceRow = record.row;
	if (sourceRow != ArchetypeRows::missing) {
		for (const auto& [type, source] : from.mComponentArrays) {
			const auto destination = to->mComponentArrays.find(type);
			if (destination != to->mComponentArrays.end() && std::ranges::find(replaced, type) == replaced.end()) {
				source->appendIndexedMove(sourceRow, *destination->second);
			}
		}
		eraseIndexedEntity(from, sourceRow);
	}

	record.row = ArchetypeRows::missing;
	if (!to->mComponentArrays.empty()) {
		auto& entities = to->mRows->entities;
		record.row = entities.size();
		entities.push_back(entity);
	}
	record.archetype = to;
}

void ComponentManager::eraseIndexedEntity(Archetype& archetype, std::size_t row) noexcept {
	for (const auto& [type, column] : archetype.mComponentArrays) {
		column->eraseIndexedRow(row);
	}
	auto& entities = archetype.mRows->entities;
	if (row + 1 != entities.size()) {
		const auto moved = entities.back();
		entities[row] = moved;
		mEntityRecords.at(moved).row = row;
	}
	entities.pop_back();
}

std::shared_ptr<Archetype> ComponentManager::getSingleComponentTransition(const std::shared_ptr<Archetype>& from, ComponentType type, bool adding) {
	if (from->mMask.test(type.value()) == adding) {
		return from;
	}
	if (const auto cached = from->mTransitions.find(type); cached != from->mTransitions.end()) {
		if (auto to = cached->second.lock()) {
			return to;
		}
	}

	auto mask = from->mMask;
	mask.set(type.value(), adding);
	auto to = getArchetype(std::move(mask));
	from->mTransitions.insert_or_assign(type, to);
	return to;
}

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
