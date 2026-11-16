#include "componentManager.h"

#include <algorithm>

namespace IKIGAI::ECS {
ComponentManager::ComponentManager()
	: ComponentManager(ArchetypeLayout::PerColumn) { }

ComponentManager::ComponentManager(ArchetypeLayout layout)
	: mIndexedRecords(layout == ArchetypeLayout::SharedRows ? std::make_shared<EntityRecords>() : nullptr)
	, mEntityRecords(mIndexedRecords ? *mIndexedRecords : mLocalRecords) { }

const std::shared_ptr<MEMORY::IAllocator>& ComponentManager::indexedChunkResource() {
	using namespace MEMORY;
	static const std::shared_ptr<IAllocator> resource = std::make_shared<OptionalProfiledAllocator<HeapAllocator, Category::Component>>();
	return resource;
}

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
	if (mErasePageIndex == pageIndex) {
		mErasePage = nullptr;
	}
	const auto branchIndex = pageIndex >> (2 * pageBits);
	const auto pagesIndex = (pageIndex >> pageBits) & pageMask;
	auto& branch = *mRoot->children[branchIndex];
	if (branch.children[pagesIndex]->remove(pageIndex & pageMask) && branch.remove(pagesIndex)) {
		mRoot->remove(branchIndex);
	}
}

bool ComponentManager::EntityRecords::insertSuppliedSlow(std::uint32_t id, Archetype* archetype, std::uint64_t generation) {
	const auto pageIndex = id >> pageBits;
	auto* page = mInsertPage && mInsertPageIndex == pageIndex ? mInsertPage : findPage(pageIndex);
	const auto run = mSuppliedRun != 0 && id == mLastSuppliedId + 1 ? std::min(mSuppliedRun + 1, denseRunLength) : 1u;
	if (page || run == denseRunLength) {
		if (mCompact && id <= mLargestCompactId && mCompact->contains(id)) {
			return false;
		}
		auto& target = page ? *page : ensurePage(pageIndex);
		auto& record = target.records[id & pageMask];
		if (record.generation != 0) {
			return false;
		}
		setSlot(target, record, archetype, generation);
	}
	else {
		if (!mCompact) {
			mCompact = std::make_unique<CompactRecords>();
		}
		if (!mCompact->try_emplace(id, Record { .archetype = archetype, .generation = generation }).second) {
			return false;
		}
		mLargestCompactId = std::max(mLargestCompactId, id);
	}
	mSuppliedRun = run;
	mLastSuppliedId = id;
	return true;
}

const Record* ComponentManager::EntityRecords::findCompact(std::uint32_t id) const noexcept {
	const auto record = mCompact->find(id);
	return record == mCompact->end() ? nullptr : &record->second;
}

Record& ComponentManager::EntityRecords::at(Entity entity) {
	if (const auto* record = find(entity)) {
		return *const_cast<Record*>(record);
	}
	throw std::out_of_range("Entity is not registered in this ECS world");
}

void ComponentManager::EntityRecords::erase(Entity entity) noexcept {
	const auto location = locateForErase(entity);
	if (location.record) {
		eraseLocated(entity, location);
	}
}

void ComponentManager::EntityRecords::eraseLocated(Entity entity, Location location) noexcept {
	const auto id = entity.getUniqueId().value();
	if (auto* page = location.page) {
		page->records[id & pageMask] = {};
		if (--page->live == 0) {
			releasePage(id >> pageBits);
		}
		return;
	}
	mCompact->erase(id);
	if (mCompact->empty()) {
		mCompact.reset();
		mLargestCompactId = 0;
	}
}

void ComponentManager::prepareIndexedTransition(const Record& record, Archetype& to) {
	if (record.archetype == &to || to.mIndexedColumns.empty()) {
		return;
	}
	auto& entities = to.mRows->entities;
	if (entities.size() < to.mReservedRows) {
		return;
	}
	reserveIndexedRows(to, entities.size() + 1);
}

void ComponentManager::reserveIndexedRows(Archetype& table, std::size_t required) {
	if (required <= table.mReservedRows) {
		return;
	}
	auto& entities = table.mRows->entities;
	if (required > entities.capacity()) {
		entities.reserve(std::max(required, entities.size() + entities.size() / 2 + 1));
	}
	auto reserved = entities.capacity();
	for (auto* column : table.mIndexedColumns) {
		reserved = std::min(reserved, column->reserveIndexedRows(required));
	}
	table.mReservedRows = reserved;
}

std::optional<ComponentManager::IndexedBatch> ComponentManager::prepareIndexedBatch(std::span<const Entity> entities, ComponentType type) {
	IndexedBatch batch;
	batch.entries.reserve(entities.size());
	std::unordered_map<Archetype*, std::uint32_t> sourceTargets;
	Archetype* previousSource = nullptr;
	std::uint32_t targetIndex = 0;
	EntityRecords::PageCursor cursor;
	for (const auto entity : entities) {
		auto* record = const_cast<Record*>(mEntityRecords.find(entity, cursor));
		if (!record) {
			throw std::out_of_range("Entity is not registered in this ECS world");
		}
		auto* source = record->archetype;
		if (source != previousSource) {
			const auto [target, inserted] = sourceTargets.try_emplace(source, static_cast<std::uint32_t>(batch.targets.size()));
			if (inserted) {
				const auto* plan = getSingleComponentTransition(source, type, true);
				// Custom moves and destruction may run user code while rows are in flight.
				if (plan && !std::ranges::all_of(source->mIndexedColumns, &ComponentArrayInterface::hasTrivialLifecycle)) {
					return std::nullopt;
				}
				auto* table = plan ? plan->target : source;
				batch.targets.push_back({ table, plan, table->mComponentArrays.at(type).get(), 0 });
			}
			targetIndex = target->second;
			previousSource = source;
		}
		auto& target = batch.targets[targetIndex];
		target.incoming += target.plan != nullptr;
		batch.entries.push_back({ record, entity, targetIndex });
	}
	for (const auto& target : batch.targets) {
		if (target.incoming != 0) {
			const auto& rows = target.table->mRows->entities;
			if (target.incoming > rows.max_size() - rows.size()) {
				throw std::length_error("ECS batch exceeds archetype capacity");
			}
			reserveIndexedRows(*target.table, rows.size() + target.incoming);
		}
	}
	return batch;
}

void ComponentManager::commitIndexedTransition(
	Entity entity, Record& record, Archetype* to, std::span<const ComponentType> replaced, const Archetype::Transition* plan) noexcept {
	auto& from = *record.archetype;
	if (&from == to) {
		return;
	}
	const auto sourceRow = record.row;
	if (sourceRow != ArchetypeRows::missing) {
		if (plan) {
			for (const auto& column : plan->columns) {
				column.source->appendIndexedMove(sourceRow, *column.destination);
			}
		}
		else {
			for (const auto& [type, source] : from.mComponentArrays) {
				const auto destination = to->mComponentArrays.find(type);
				if (destination != to->mComponentArrays.end() && std::ranges::find(replaced, type) == replaced.end()) {
					source->appendIndexedMove(sourceRow, *destination->second);
				}
			}
		}
		eraseIndexedEntity(from, sourceRow);
	}

	record.row = ArchetypeRows::missing;
	if (!to->mIndexedColumns.empty()) {
		auto& entities = to->mRows->entities;
		record.row = entities.size();
		entities.push_back(entity);
	}
	record.archetype = to;
}

void ComponentManager::eraseIndexedEntity(Archetype& archetype, std::size_t row) noexcept {
	for (auto* column : archetype.mIndexedColumns) {
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

const Archetype::Transition* ComponentManager::getSingleComponentTransition(Archetype* from, ComponentType type, bool adding) {
	if (const auto* last = from->mLastTransition; last && last->type == type && last->adding == adding) {
		return last;
	}
	if (from->mMask.test(type.value()) == adding) {
		return nullptr;
	}
	if (const auto cached = from->mTransitions.find(type); cached != from->mTransitions.end()) {
		return from->mLastTransition = &cached->second;
	}

	auto mask = from->mMask;
	mask.set(type.value(), adding);
	auto* to = getArchetype(std::move(mask)).get();
	Archetype::Transition transition { .target = to, .type = type, .adding = adding, .columns = {} };
	if (from->mRows) {
		transition.columns.reserve(std::min(from->mComponentArrays.size(), to->mComponentArrays.size()));
		for (const auto& [type, source] : from->mComponentArrays) {
			if (const auto destination = to->mComponentArrays.find(type); destination != to->mComponentArrays.end()) {
				transition.columns.push_back({ source.get(), destination->second.get() });
			}
		}
	}
	const auto insert = from->mTransitions.emplace(type, std::move(transition));
	return from->mLastTransition = &insert.first->second;
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
