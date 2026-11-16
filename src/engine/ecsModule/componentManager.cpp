#include "componentManager.h"

#include <algorithm>
#include <bit>

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

std::optional<ComponentManager::IndexedBatchRange> ComponentManager::prepareIndexedBatchRange(std::span<const Entity> entities, ComponentType type) {
	auto& record = mEntityRecords.at(entities.front());
	auto* source = record.archetype;
	const auto sourceRows = std::span(source->mRows->entities);
	const auto first = record.row;
	if (first > sourceRows.size() || entities.size() > sourceRows.size() - first) {
		return std::nullopt;
	}
	if (!std::ranges::equal(entities, sourceRows.subspan(first, entities.size()))) {
		return std::nullopt;
	}
	if (!std::ranges::all_of(source->mIndexedColumns, &ComponentArrayInterface::hasTrivialLifecycle)) {
		return std::nullopt;
	}
	const auto* plan = getSingleComponentTransition(source, type, true);
	if (!plan) {
		return std::nullopt;
	}
	const auto& targetRows = plan->target->mRows->entities;
	if (entities.size() > targetRows.max_size() - targetRows.size()) {
		throw std::length_error("ECS batch exceeds archetype capacity");
	}
	reserveIndexedRows(*plan->target, targetRows.size() + entities.size());
	return IndexedBatchRange { source, plan, first };
}

void ComponentManager::commitIndexedBatchRange(const IndexedBatchRange& range, std::size_t count) noexcept {
	auto& sourceRows = range.source->mRows->entities;
	auto& targetRows = range.plan->target->mRows->entities;
	const auto targetFirst = targetRows.size();
	const auto sourceFirst = sourceRows.begin() + range.first;
	targetRows.insert(targetRows.end(), sourceFirst, sourceFirst + count);
	for (const auto& column : range.plan->columns) {
		column.source->transferIndexedRange(range.first, count, *column.destination);
	}

	const auto tailCount = std::min(count, sourceRows.size() - range.first - count);
	std::copy_n(sourceRows.end() - tailCount, tailCount, sourceFirst);
	sourceRows.resize(sourceRows.size() - count);

	EntityRecords::PageCursor cursor;
	for (std::size_t offset = 0; offset < tailCount; ++offset) {
		const auto index = range.first + offset;
		mEntityRecords.at(sourceRows[index], cursor).row = index;
	}
	for (std::size_t offset = 0; offset < count; ++offset) {
		const auto index = targetFirst + offset;
		auto& record = mEntityRecords.at(targetRows[index], cursor);
		record.archetype = range.plan->target;
		record.row = index;
	}
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
				batch.targets.push_back({ source, table, plan, table->mComponentArrays.at(type).get(), 0 });
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

std::optional<std::vector<ComponentManager::IndexedScatterTarget>> ComponentManager::prepareIndexedScatter(
	const IndexedBatch& batch, std::size_t chunkSize, ScatterOrder order) {
	std::vector<IndexedScatterTarget> scatter;
	scatter.reserve(batch.targets.size());
	constexpr std::size_t minimumRetainedColumns = 4;
	for (const auto& target : batch.targets) {
		const auto rows = target.source->mRows->entities.size();
		const auto chunks = rows / chunkSize + (rows % chunkSize != 0);
		if (!target.plan || target.plan->columns.size() < minimumRetainedColumns || target.incoming < chunkSize || target.incoming > rows
			|| chunks > target.incoming) {
			return std::nullopt;
		}
		auto& group = scatter.emplace_back();
		group.outgoing.reserve(target.incoming);
	}

	for (const auto& edit : batch.entries) {
		const auto row = static_cast<std::uint32_t>(edit.record->row);
		auto& group = scatter[edit.targetIndex];
		if (!group.outgoing.empty() && row < group.outgoing.back().source) {
			group.needsGrouping = true;
		}
		group.outgoing.push_back({ row, static_cast<std::uint32_t>(group.outgoing.size()) });
	}

	std::vector<std::size_t> chunks;
	std::vector<IndexedRowMove> reordered;
	std::vector<unsigned char> seen(chunkSize);
	std::vector<unsigned char> outgoingTail;
	std::vector<std::uint64_t> selected;
	constexpr std::size_t minimumWideColumns = 8;
	// Limit the dense destination-offset scratch to batches near the measured density.
	constexpr std::size_t minimumDensityDivisor = 16;
	constexpr std::size_t maximumDensityDivisor = 8;
	for (std::size_t index = 0; index < scatter.size(); ++index) {
		auto& group = scatter[index];
		const auto& target = batch.targets[index];
		const auto rows = target.source->mRows->entities.size();
		const auto count = group.outgoing.size();
		const auto minimumCount = rows / minimumDensityDivisor + (rows % minimumDensityDivisor != 0);
		const bool wideSparseBatch = group.needsGrouping && target.plan->columns.size() >= minimumWideColumns && count >= minimumCount
			&& count <= rows / maximumDensityDivisor;
		const bool ascendingSource = order == ScatterOrder::SourceRowsBitmap || (order == ScatterOrder::Adaptive && wideSparseBatch);
		group.destinationOrder = order == ScatterOrder::Destination;
		const auto chunkCount = rows / chunkSize + (rows % chunkSize != 0);
		if (group.needsGrouping && !group.destinationOrder && !ascendingSource) {
			chunks.assign(chunkCount, 0);
			for (const auto& move : group.outgoing) {
				++chunks[move.source / chunkSize];
			}
			std::size_t offset = 0;
			for (auto& count : chunks) {
				const auto next = offset + count;
				count = offset;
				offset = next;
			}
			reordered.resize(group.outgoing.size());
			for (const auto& move : group.outgoing) {
				reordered[chunks[move.source / chunkSize]++] = move;
			}
			group.outgoing.swap(reordered);
		}

		const auto retained = rows - group.outgoing.size();
		group.compacted.reserve(std::min(retained, group.outgoing.size()));
		if (group.destinationOrder || ascendingSource) {
			constexpr std::size_t bitsPerWord = 64;
			selected.assign(rows / bitsPerWord + (rows % bitsPerWord != 0), 0);
			// Read destination offsets only for bits present in selected.
			auto destinations = ascendingSource ? std::make_unique_for_overwrite<std::uint32_t[]>(rows) : nullptr;
			for (const auto& move : group.outgoing) {
				auto& word = selected[move.source / bitsPerWord];
				const auto bit = std::uint64_t { 1 } << (move.source % bitsPerWord);
				if (word & bit) {
					return std::nullopt;
				}
				word |= bit;
				if (ascendingSource) {
					destinations[move.source] = move.destination;
				}
			}
			const auto retainedWords = retained / bitsPerWord + (retained % bitsPerWord != 0);
			const auto words = ascendingSource ? selected.size() : retainedWords;
			reordered.clear();
			if (ascendingSource) {
				reordered.reserve(group.outgoing.size());
			}
			auto tail = rows;
			for (std::size_t wordIndex = 0; wordIndex < words; ++wordIndex) {
				auto pending = selected[wordIndex];
				const auto first = wordIndex * bitsPerWord;
				while (pending) {
					const auto row = static_cast<std::uint32_t>(first + std::countr_zero(pending));
					pending &= pending - 1;
					if (ascendingSource) {
						reordered.push_back({ row, destinations[row] });
					}
					if (row < retained) {
						do {
							--tail;
						} while ((selected[tail / bitsPerWord] >> (tail % bitsPerWord)) & 1);
						group.compacted.push_back({ static_cast<std::uint32_t>(tail), row });
					}
				}
			}
			if (ascendingSource) {
				group.outgoing.swap(reordered);
			}
			continue;
		}

		outgoingTail.assign(group.outgoing.size(), 0);
		auto chunk = chunkCount;
		for (const auto& move : group.outgoing) {
			if (chunk != move.source / chunkSize) {
				chunk = move.source / chunkSize;
				std::ranges::fill(seen, 0);
			}
			if (std::exchange(seen[move.source % chunkSize], 1)) {
				return std::nullopt;
			}
			if (move.source >= retained) {
				outgoingTail[move.source - retained] = 1;
			}
		}

		// Consume surviving tail rows once, leaving rows in the retained prefix in place.
		auto tail = outgoingTail.size();
		for (const auto& move : group.outgoing) {
			if (move.source < retained) {
				do {
					--tail;
				} while (outgoingTail[tail]);
				group.compacted.push_back({ static_cast<std::uint32_t>(retained + tail), move.source });
			}
		}
	}
	return scatter;
}

void ComponentManager::appendIndexedScatterRows(const IndexedBatch& batch, std::span<IndexedScatterTarget> scatter) noexcept {
	for (std::size_t index = 0; index < scatter.size(); ++index) {
		const auto& target = batch.targets[index];
		auto& group = scatter[index];
		auto& sourceRows = target.source->mRows->entities;
		auto& targetRows = target.table->mRows->entities;
		const auto first = targetRows.size();
		group.nextDestination = static_cast<std::uint32_t>(first);
		targetRows.resize(first + group.outgoing.size());
		if (group.destinationOrder) {
			auto rows = targetRows.begin() + first;
			for (const auto& move : group.outgoing) {
				*rows++ = sourceRows[move.source];
			}
		}
		else {
			for (const auto& move : group.outgoing) {
				targetRows[first + move.destination] = sourceRows[move.source];
			}
		}
		for (const auto& column : target.plan->columns) {
			column.source->appendIndexedRows(group.outgoing, *column.destination, group.destinationOrder);
		}
	}
	for (const auto& edit : batch.entries) {
		edit.record->archetype = batch.targets[edit.targetIndex].table;
		edit.record->row = scatter[edit.targetIndex].nextDestination++;
	}
}

void ComponentManager::compactIndexedScatter(const IndexedBatch& batch, std::span<const IndexedScatterTarget> scatter) noexcept {
	EntityRecords::PageCursor cursor;
	for (std::size_t index = 0; index < scatter.size(); ++index) {
		const auto& target = batch.targets[index];
		const auto& group = scatter[index];
		auto& sourceRows = target.source->mRows->entities;
		const auto retained = sourceRows.size() - group.outgoing.size();
		for (const auto& column : target.plan->columns) {
			column.source->compactIndexedRows(retained, group.compacted);
		}
		for (const auto& move : group.compacted) {
			const auto entity = sourceRows[move.source];
			sourceRows[move.destination] = entity;
			mEntityRecords.at(entity, cursor).row = move.destination;
		}
		sourceRows.resize(retained);
	}
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
