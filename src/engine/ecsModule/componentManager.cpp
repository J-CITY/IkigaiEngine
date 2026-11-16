#include "componentManager.h"

namespace IKIGAI::ECS {
ComponentManager::ComponentManager()
	: ComponentManager(ArchetypeLayout::PerColumn) { }

ComponentManager::ComponentManager(ArchetypeLayout layout)
	: mIndexedRecords(layout == ArchetypeLayout::SharedRows ? std::make_shared<EntityRecords>() : nullptr)
	, mEntityRecords(mIndexedRecords ? *mIndexedRecords : mLocalRecords) { }

ComponentManager::EntityRecords::Page& ComponentManager::EntityRecords::ensurePage(std::uint32_t pageIndex) {
	if (!mRoot) {
		mRoot = std::make_unique<Root>();
	}
	auto& branch = mRoot->ensure(pageIndex >> (2 * pageBits));
	auto& pages = branch.ensure((pageIndex >> pageBits) & pageMask);
	auto& page = pages.ensure(pageIndex & pageMask);
	mInsertPage = &page;
	mInsertPageIndex = pageIndex;
	return page;
}

void ComponentManager::EntityRecords::releasePage(std::uint32_t pageIndex) noexcept {
	if (mInsertPageIndex == pageIndex) {
		mInsertPage = nullptr;
	}
	const auto branchIndex = pageIndex >> (2 * pageBits);
	const auto pagesIndex = (pageIndex >> pageBits) & pageMask;
	auto& branch = *mRoot->children[branchIndex];
	if (branch.children[pagesIndex]->remove(pageIndex & pageMask) && branch.remove(pagesIndex)) {
		mRoot->remove(branchIndex);
	}
}

Record& ComponentManager::EntityRecords::at(Entity entity) {
	if (const auto* record = find(entity)) {
		return *const_cast<Record*>(record);
	}
	throw std::out_of_range("Entity is not registered in this ECS world");
}

void ComponentManager::EntityRecords::erase(Entity entity) noexcept {
	const auto id = entity.getUniqueId().value();
	const auto pageIndex = id >> pageBits;
	if (auto* page = findPage(pageIndex)) {
		auto& record = page->records[id & pageMask];
		if (record.generation != 0) {
			record = {};
			if (--page->live == 0) {
				releasePage(pageIndex);
			}
		}
	}
}

void ComponentManager::prepareIndexedTransition(const Record& record, Archetype& to) {
	if (record.archetype == &to || to.mComponentArrays.empty()) {
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

void ComponentManager::commitIndexedTransition(Entity entity, Record& record, Archetype* to, std::span<const ComponentType> replaced) noexcept {
	auto& from = *record.archetype;
	if (&from == to) {
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

Archetype* ComponentManager::getSingleComponentTransition(Archetype* from, ComponentType type, bool adding) {
	if (from->mMask.test(type.value()) == adding) {
		return from;
	}
	if (const auto cached = from->mTransitions.find(type); cached != from->mTransitions.end()) {
		return cached->second;
	}

	auto mask = from->mMask;
	mask.set(type.value(), adding);
	auto* to = getArchetype(std::move(mask)).get();
	from->mTransitions.emplace(type, to);
	return to;
}

ComponentArrayInterface* ComponentManager::findArray(Entity entity, ComponentType type, std::uint64_t entityGeneration) const {
	const auto* record = mEntityRecords.find(entity);
	if (!record || (entityGeneration != 0 && record->generation != entityGeneration)) {
		return nullptr;
	}
	const auto& columns = record->archetype->mComponentArrays;
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
