#pragma once
#include "access.h"
#include "commandBuffer.h"
#include "events.h"
#include "relationManager.h"
#include "signature.h"
#include "systemAccessScope.h"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

#include "componentArray.h"
#include "componentHandle.h"
#include "entityManager.h"
#include "utilsModule/idGenerator.h"
#include "utilsModule/singleton.h"

#include "utilsModule/log/loggerDefine.h"
#include <iostream>
#include <tuple>

namespace IKIGAI::ECS {
// TODO: use faster std container (EA for example)
// TODO: use allocators for containers

class ComponentManager;

using TagType = ComponentType;

enum class StorageType { Archetype, Sparse };

class /*alignas(32)*/ Component : public UTILS::ControlBlockHandler {
public:
	Component() = default;
	virtual ~Component() override = default;

	template <class T> UTILS::WeakPtr<T> getWeak() {
		return UTILS::WeakPtr<T>(*this);
	}
};

class Archetype {
public:
	Signature mMask;
	std::unordered_map<ComponentType, std::shared_ptr<ComponentArrayInterface>> mComponentArrays;

	template <class T> std::shared_ptr<ComponentArray<T>> getComponents(ComponentType type) {
		if (!mComponentArrays.contains(type)) {
			return nullptr;
		}
		return std::static_pointer_cast<ComponentArray<T>>(mComponentArrays.at(type));
	}

	size_t size() const {
		return mRows ? mRows->entities.size() : (mComponentArrays.empty() ? 0 : mComponentArrays.begin()->second->getSize());
	}

private:
	friend class ComponentManager;
	friend struct EcsBenchmarkAccess;
	std::shared_ptr<ArchetypeRows> mRows;
	std::vector<ComponentArrayInterface*> mIndexedColumns;
	std::size_t mReservedRows = 0;

	struct ColumnTransfer {
		ComponentArrayInterface* source = nullptr;
		ComponentArrayInterface* destination = nullptr;
	};
	struct Transition {
		Archetype* target = nullptr;
		ComponentType type {};
		bool adding = false;
		std::vector<ColumnTransfer> columns;
	};
	// For a fixed source signature and type, a transition toggles that type.
	std::unordered_map<ComponentType, Transition> mTransitions;
	const Transition* mLastTransition = nullptr;
};

struct Record {
	using Tags = std::unordered_map<ComponentType, std::unordered_map<TagType, std::any>>;
	Archetype* archetype = nullptr;
	std::uint64_t generation = 0;
	std::size_t row = ArchetypeRows::missing;
	std::unique_ptr<Tags> mTagsMap {};
};

class ComponentManager {
	friend class World;
	friend struct EcsBenchmarkAccess;

	class EntityRecords {
		// ID bytes select a branch, a directory, a page and a slot.
		// Only occupied branches allocate storage. Pages keep record addresses stable.
		static constexpr std::uint32_t pageBits = 8;
		static constexpr std::uint32_t pageSize = 1u << pageBits;
		static constexpr std::uint32_t pageMask = pageSize - 1;
		// Promote a stream of supplied IDs only after evidence of a dense run.
		static constexpr std::uint32_t denseRunLength = 8;

		struct Page {
			std::array<Record, pageSize> records;
			std::size_t live = 0;
		};

		template <class Child> struct Directory {
			std::array<std::unique_ptr<Child>, pageSize> children {};
			std::size_t used = 0;

			Child& ensure(std::uint32_t index) {
				auto& child = children[index];
				if (!child) {
					child = std::make_unique<Child>();
					++used;
				}
				return *child;
			}

			bool remove(std::uint32_t index) noexcept {
				children[index].reset();
				return --used == 0;
			}
		};

		using Pages = Directory<Page>;
		using Branch = Directory<Pages>;
		using Root = Directory<Branch>;
		using CompactRecords = std::unordered_map<std::uint32_t, Record>;

		std::unique_ptr<Root> mRoot;
		Page* mInsertPage = nullptr;
		std::uint32_t mInsertPageIndex = 0;
		std::uint32_t mErasePageIndex = 0;
		Page* mErasePage = nullptr;
		std::unique_ptr<CompactRecords> mCompact {};
		std::uint32_t mLargestCompactId = 0;
		std::uint32_t mLastSuppliedId = 0;
		std::uint32_t mSuppliedRun = 0;

		Page& ensurePage(std::uint32_t pageIndex);
		void releasePage(std::uint32_t pageIndex) noexcept;
		const Record* findCompact(std::uint32_t id) const noexcept;
		bool insertSuppliedSlow(std::uint32_t id, Archetype* archetype, std::uint64_t generation);

		Page* findPage(std::uint32_t pageIndex) const noexcept {
			const auto* branch = mRoot ? mRoot->children[pageIndex >> (2 * pageBits)].get() : nullptr;
			const auto* pages = branch ? branch->children[(pageIndex >> pageBits) & pageMask].get() : nullptr;
			return pages ? pages->children[pageIndex & pageMask].get() : nullptr;
		}

		static void setSlot(Page& page, Record& record, Archetype* archetype, std::uint64_t generation) noexcept {
			record.archetype = archetype;
			record.generation = generation;
			++page.live;
		}

	public:
		struct Location {
			const Record* record = nullptr;
			Page* page = nullptr;
		};

		// Each query owns its cursor. Structural changes are blocked during the pass.
		struct PageCursor {
			std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
			Page* page = nullptr;
		};

		void insertGenerated(Entity entity, Archetype* archetype, std::uint64_t generation) {
			// Observing a loaded ID advances the generator before it can issue that ID.
			const auto id = entity.getUniqueId().value();
			const auto pageIndex = id >> pageBits;
			auto& page = mInsertPage && mInsertPageIndex == pageIndex ? *mInsertPage : ensurePage(pageIndex);
			setSlot(page, page.records[id & pageMask], archetype, generation);
		}

		bool insertSupplied(Entity entity, Archetype* archetype, std::uint64_t generation) {
			const auto id = entity.getUniqueId().value();
			if (mInsertPage && mInsertPageIndex == (id >> pageBits) && mSuppliedRun == denseRunLength && id == mLastSuppliedId + 1
				&& id > mLargestCompactId) {
				auto& record = mInsertPage->records[id & pageMask];
				if (record.generation != 0) {
					return false;
				}
				setSlot(*mInsertPage, record, archetype, generation);
				mLastSuppliedId = id;
				return true;
			}
			return insertSuppliedSlow(id, archetype, generation);
		}

	private:
		Location locateInPage(std::uint32_t id, Page* page) const noexcept {
			const auto* record = page ? &page->records[id & pageMask] : nullptr;
			if (record && record->generation != 0) {
				return { record, page };
			}
			return { mCompact ? findCompact(id) : nullptr, nullptr };
		}

	public:
		Location locateForErase(Entity entity) noexcept {
			const auto id = entity.getUniqueId().value();
			const auto pageIndex = id >> pageBits;
			if (mErasePageIndex != pageIndex || !mErasePage) {
				mErasePage = findPage(pageIndex);
				mErasePageIndex = pageIndex;
			}
			return locateInPage(id, mErasePage);
		}

		const Record* find(Entity entity) const noexcept {
			const auto id = entity.getUniqueId().value();
			return locateInPage(id, findPage(id >> pageBits)).record;
		}

		const Record* find(Entity entity, PageCursor& cursor) const noexcept {
			const auto id = entity.getUniqueId().value();
			const auto pageIndex = id >> pageBits;
			if (cursor.index != pageIndex) {
				cursor.index = pageIndex;
				cursor.page = findPage(pageIndex);
			}
			return locateInPage(id, cursor.page).record;
		}

		Record& at(Entity entity);
		void erase(Entity entity) noexcept;
		void eraseLocated(Entity entity, Location location) noexcept;
	};

public:
	ComponentManager();
	explicit ComponentManager(ArchetypeLayout layout);

private:
	std::unordered_map<Entity, Signature> mSignatures;

	const EntityHandle::OwnerId mOwner = IdGenerator<EntityHandle>::generateId();
	std::uint64_t mNextEntityGeneration = 1;
	std::atomic_size_t mActiveQueries { 0 };

	class QueryScope {
	public:
		explicit QueryScope(std::atomic_size_t& activeQueries)
			: mActiveQueries(activeQueries) {
			mActiveQueries.fetch_add(1, std::memory_order_relaxed);
		}

		~QueryScope() {
			mActiveQueries.fetch_sub(1, std::memory_order_relaxed);
		}

		QueryScope(const QueryScope&) = delete;
		QueryScope& operator=(const QueryScope&) = delete;

	private:
		std::atomic_size_t& mActiveQueries;
	};

	void requireStructuralChangesAllowed() const {
		if (!immediateStructuralChangesAllowed() || mActiveQueries.load(std::memory_order_relaxed) != 0) {
			throw std::logic_error("ECS structural changes must be deferred during queries and shared system batches");
		}
	}

	// Archetype structs
	// Table ownership covers all records and transition entries in this world.
	std::unordered_map<Signature, std::shared_ptr<Archetype>> mMaskToArchetype;
	EntityRecords mLocalRecords;
	std::shared_ptr<EntityRecords> mIndexedRecords;
	EntityRecords& mEntityRecords;
	Archetype* mEmptyArchetype = nullptr;
	std::unordered_map<ComponentType, std::shared_ptr<ComponentArrayInterface>> mDefaultArraysArchetype;

	std::unordered_map<ComponentType, std::unique_ptr<Component>> mSingletonComponent;

	// Hybrid Storage
	std::unordered_map<ComponentType, StorageType> mComponentStorageType;
	std::unordered_map<ComponentType, std::shared_ptr<ComponentArrayInterface>> mSparseArrays;

	// Relations
	RelationManager mRelationManager;
	EventManager mEventManager;

private:
private:
	// Helper to lazily register component with default storage if not already registered
	template <typename T> StorageType ensureComponentRegistered(ComponentType id) {
		if (const auto storage = mComponentStorageType.find(id); storage != mComponentStorageType.end()) {
			return storage->second;
		}

		// Default storage is Archetype. Sparse is opt-in via registerComponent.
		registerComponent<T>(StorageType::Archetype);
		return StorageType::Archetype;
	}

	template <typename T, typename... Args> void addComponentToArchetypeImpl(Entity entity, Archetype& to, T&& component) {
		using C = std::decay_t<T>;
		const auto cname = getComponentType<C>();

		// We assume this is only called for Archetype components
		// But careful validation or design is needed.
		// Ideally addComponents logic separates them.

		if (const auto column = to.mComponentArrays.find(cname); column != to.mComponentArrays.end()) {
			static_cast<ComponentArray<C>&>(*column->second).insert(entity, std::forward<T>(component));
		}
	}

	template <typename T> void addComponentToArchetypeMaskImpl(Signature& mask) {
		using C = std::decay_t<T>;
		ComponentType id = getComponentType<C>();
		if (ensureComponentRegistered<C>(id) == StorageType::Archetype) {
			mask.set(static_cast<size_t>(id), true);
		}
	}

	template <typename... Component> void addComponentsToArchetypeMask(Signature& mask) {
		(addComponentToArchetypeMaskImpl<Component>(mask), ...);
	}

	template <typename T> void addComponentToSparseImpl(Entity entity, T&& component) {
		using C = std::decay_t<T>;
		ComponentType id = getComponentType<C>();
		if (ensureComponentRegistered<C>(id) == StorageType::Sparse) {
			std::static_pointer_cast<ComponentArray<C>>(mSparseArrays[id])->insert(entity, std::forward<T>(component));
		}
	}

	template <typename T> void removeComponentFromArchetypeMaskImpl(Signature& mask) {
		using C = std::decay_t<T>;
		ComponentType id = getComponentType<C>();
		if (mComponentStorageType.contains(id) && mComponentStorageType[id] == StorageType::Archetype) {
			mask.set(static_cast<size_t>(id), false);
		}
	}

	template <typename... Component> void removeComponentsFromArchetypeMask(Signature& mask) {
		(removeComponentFromArchetypeMaskImpl<Component>(mask), ...);
	}

	template <typename T> void removeComponentFromSparseImpl(Entity entity) {
		using C = std::decay_t<T>;
		ComponentType id = getComponentType<C>();
		if (const auto pool = mSparseArrays.find(id); pool != mSparseArrays.end()) {
			pool->second->entityDestroyed(entity);
		}
	}

	template <typename... Component> void addComponentsToArchetype(Entity entity, Archetype& to, Component&&... components) {
		(addComponentToArchetypeImpl<Component>(entity, to, components), ...);
	}

	// Migrate archetype columns. NewComponents are not transferred: the caller inserts each once.
	// An empty pack (removeComponents) transfers every column that still exists on `to`.
	template <class... NewComponents> void moveEntity(Entity entity, Archetype* from, Archetype* to, Record& record) {
		const bool sameArchetype = from == to;
		if (sameArchetype && !((from->mComponentArrays.contains(getComponentType<std::decay_t<NewComponents>>())) || ...)) {
			return;
		}

		for (auto& [ctype, fromArr] : from->mComponentArrays) {
			if (!fromArr->contains(entity)) {
				continue;
			}
			const bool inPack = ((getComponentType<std::decay_t<NewComponents>>() == ctype) || ...);
			const auto destination = to->mComponentArrays.find(ctype);
			if (inPack || destination == to->mComponentArrays.end()) {
				fromArr->entityDestroyed(entity);
			}
			else {
				// Same table: replay remove+append so a replaced column stays index-aligned.
				fromArr->transferEntityTo(entity, *destination->second);
			}
		}

		if (!sameArchetype) {
			for (auto& [ctype, fromArr] : from->mComponentArrays) {
				if (fromArr->contains(entity)) {
					throw std::logic_error("Entity remained in source archetype");
				}
			}
		}
		record.archetype = to;
	}

	const Archetype::Transition* getSingleComponentTransition(Archetype* from, ComponentType type, bool adding);
	static const std::shared_ptr<MEMORY::IAllocator>& indexedChunkResource();
	void prepareIndexedTransition(const Record& record, Archetype& to);
	void commitIndexedTransition(
		Entity entity, Record& record, Archetype* to, std::span<const ComponentType> replaced, const Archetype::Transition* plan = nullptr) noexcept;
	void eraseIndexedEntity(Archetype& archetype, std::size_t row) noexcept;

	template <class... Components>
	std::uint64_t addIndexedComponents(Entity entity, Record& record, Archetype* to, std::span<const ComponentType> types,
		const Archetype::Transition* plan, Components&... values) {
		constexpr auto count = sizeof...(Components);
		std::array<ComponentArrayInterface*, count> columns {};
		std::array<std::uint64_t, count> generations {};
		std::size_t index = 0;
		const auto stage = [&]<class T>(T&) {
			const auto i = index++;
			const auto type = types[i];
			if (const auto column = to->mComponentArrays.find(type); column != to->mComponentArrays.end()) {
				columns[i] = column->second.get();
				generations[i] = ComponentArray<T>::nextGeneration();
			}
			else {
				columns[i] = mSparseArrays.at(type).get();
			}
		};
		(stage(values), ...);
		prepareIndexedTransition(record, *to);

		std::array<ComponentArrayInterface*, count> insertedSparse {};
		std::size_t inserted = 0;
		index = 0;
		const auto addSparse = [&]<class T>(T& value) {
			const auto i = index++;
			if (generations[i] == 0) {
				static_cast<ComponentArray<T>&>(*columns[i]).insert(entity, std::move(value));
				insertedSparse[inserted++] = columns[i];
			}
		};
		try {
			(addSparse(values), ...);
		} catch (...) {
			while (inserted != 0) {
				insertedSparse[--inserted]->entityDestroyed(entity);
			}
			throw;
		}

		const bool replacing = record.archetype == to;
		index = 0;
		const auto addDense = [&]<class T>(T& value) noexcept {
			const auto i = index++;
			if (generations[i] != 0) {
				auto& column = static_cast<ComponentArray<T>&>(*columns[i]);
				if (replacing) {
					column.replaceIndexed(record.row, value, generations[i]);
				}
				else {
					column.appendIndexed(value, generations[i]);
				}
			}
		};
		(addDense(values), ...);
		commitIndexedTransition(entity, record, to, types, plan);
		if constexpr (count == 1) {
			return generations.front();
		}
		return 0;
	}

	template <typename T> void insertNewComponent(Entity entity, Record& record, T&& component) {
		using C = std::decay_t<T>;
		const ComponentType id = getComponentType<C>();
		ensureComponentRegistered<C>(id);
		if (mComponentStorageType.at(id) == StorageType::Sparse) {
			addComponentToSparseImpl(entity, std::forward<T>(component));
			return;
		}
		addComponentToArchetypeImpl(entity, *record.archetype, std::forward<T>(component));
	}

	template <class... Components, class Func, std::size_t... Indices>
	static void visitDense(const std::array<ComponentArrayInterface*, sizeof...(Components)>& arrays, Func& func, std::index_sequence<Indices...>) {
		const auto columns = std::tuple { static_cast<ComponentArray<RawType<Components>>*>(arrays[Indices])... };
		const auto& leading = *std::get<0>(columns);
		const std::span<const Entity> entities = leading.getEntities();
		if (((std::get<Indices>(columns)->componentArray.size() != entities.size()) || ...)) {
			throw std::logic_error("Archetype column sizes diverged");
		}

		// Every column in an archetype stores the same rows with the same chunk boundaries.
		const auto chunkCount = leading.componentArray.chunk_count();
		std::size_t firstRow = 0;
		for (std::size_t chunk = 0; chunk < chunkCount; ++chunk) {
			const auto rows = std::tuple { std::get<Indices>(columns)->componentArray.chunk(chunk)... };
			const auto rowCount = std::get<0>(rows).size();
			for (std::size_t offset = 0; offset < rowCount; ++offset) {
				const Entity entity = entities[firstRow + offset];
				std::invoke(func, entity, static_cast<QueryReference<Components>>(std::get<Indices>(rows)[offset])...);
			}
			firstRow += rowCount;
		}
	}

	template <class... Components, class Func, std::size_t... Indices>
	static void visitJoinedRow(const std::array<ComponentArrayInterface*, sizeof...(Components)>& arrays,
		const std::array<bool, sizeof...(Components)>& rowAligned, const ComponentArrayInterface& leading, std::size_t row, const Entity entity,
		Func& func, std::index_sequence<Indices...>, std::size_t indexedRow = ArchetypeRows::missing) {
		const auto pointers = std::tuple { ([&] {
			auto& array = *static_cast<ComponentArray<RawType<Components>>*>(arrays[Indices]);
			if (indexedRow != ArchetypeRows::missing && rowAligned[Indices]) {
				return &array.componentArray[indexedRow];
			}
			return rowAligned[Indices] || arrays[Indices] == &leading ? &array.at(row) : array.tryGetData(entity);
		}())... };
		if (((std::get<Indices>(pointers) != nullptr) && ...)) {
			std::invoke(func, entity, static_cast<QueryReference<Components>>(*std::get<Indices>(pointers))...);
		}
	}

	template <class... Components, class Func>
	static void visitRows(const std::array<ComponentArrayInterface*, sizeof...(Components)>& arrays,
		const std::array<bool, sizeof...(Components)>& rowAligned, const ComponentArrayInterface& leading, Func& func) {
		const auto entities = leading.getEntities();
		for (std::size_t row = 0; row < entities.size(); ++row) {
			visitJoinedRow<Components...>(arrays, rowAligned, leading, row, entities[row], func, std::index_sequence_for<Components...> {});
		}
	}

	template <bool DenseFirst, class First, class Second, class Func>
	void visitIndexedPair(const std::array<ComponentArrayInterface*, 2>& arrays, const Archetype& table, Func& func) const {
		using DenseTerm = std::conditional_t<DenseFirst, First, Second>;
		using SparseTerm = std::conditional_t<DenseFirst, Second, First>;
		auto& dense = static_cast<ComponentArray<RawType<DenseTerm>>&>(*arrays[DenseFirst ? 0 : 1]).componentArray;
		auto& sparse = static_cast<ComponentArray<RawType<SparseTerm>>&>(*arrays[DenseFirst ? 1 : 0]);
		const auto entities = sparse.getEntities();
		EntityRecords::PageCursor cursor;
		std::size_t firstRow = 0;
		for (std::size_t chunk = 0; chunk < sparse.componentArray.chunk_count(); ++chunk) {
			const auto values = sparse.componentArray.chunk(chunk);
			for (std::size_t offset = 0; offset < values.size(); ++offset) {
				const Entity entity = entities[firstRow + offset];
				const auto* record = mEntityRecords.find(entity, cursor);
				if (record && record->archetype == &table) {
					auto& denseValue = dense[record->row];
					auto& sparseValue = values[offset];
					if constexpr (DenseFirst) {
						std::invoke(func, entity, static_cast<QueryReference<First>>(denseValue), static_cast<QueryReference<Second>>(sparseValue));
					}
					else {
						std::invoke(func, entity, static_cast<QueryReference<First>>(sparseValue), static_cast<QueryReference<Second>>(denseValue));
					}
				}
			}
			firstRow += values.size();
		}
	}

	template <template <class> class Event, class T> bool emitComponentEvent(Entity entity) {
		auto* signal = mEventManager.findSignal<Event<T>>();
		if (!signal || signal->getListenerCount() == 0) {
			return false;
		}
		auto* array = findArray<T>(entity);
		if (auto* component = array ? array->tryGetData(entity) : nullptr) {
			signal->run(Event<T> { entity, component });
		}
		return true;
	}

	template <typename T> T& getRef(Entity entity) {
		ComponentType id = getComponentType<T>();
		const auto storage = mComponentStorageType.find(id);
		if (storage != mComponentStorageType.end() && storage->second == StorageType::Archetype) {
			const Record& record = mEntityRecords.at(entity);
			const auto& archetype = record.archetype;
			return std::static_pointer_cast<ComponentArray<T>>(archetype->mComponentArrays.at(id))->getData(entity);
		}
		return std::static_pointer_cast<ComponentArray<T>>(mSparseArrays.at(id))->getData(entity);
	}

public:
	template <class First, class Func> void forEachSparse(Func&& func) {
		static_assert(!IsExclude<RawType<First>>, "Exclude is not supported by forEachSparse");
		const QueryScope query(mActiveQueries);
		const auto type = getComponentType<First>();
		SystemAccessScope::requireComponent(*this, type, AccessTraits<First>::ReadOnly ? AccessMode::Read : AccessMode::Write);
		const auto pool = mSparseArrays.find(type);
		if (pool == mSparseArrays.end()) {
			return;
		}

		auto& array = static_cast<ComponentArray<RawType<First>>&>(*pool->second);
		const auto count = static_cast<std::size_t>(array.getSize());
		for (std::size_t i = 0; i < count; ++i) {
			std::invoke(func, array.getEntity(i), static_cast<QueryReference<First>>(array.at(i)));
		}
	}

	template <typename T> ComponentType getComponentTypeImpl() const {
		static const ComponentType componentId = IdGenerator<ComponentManager>::generateId();
		// Type IDs are process-wide, like the generator: every world agrees on T.
		return componentId;
	}

	template <typename T> ComponentType getComponentType() const {
		return getComponentTypeImpl<RawType<T>>();
	}

	template <typename T> void registerComponent(StorageType type = StorageType::Archetype) {
		requireStructuralChangesAllowed();
		static_assert(std::is_same_v<T, RawType<T>> && !IsExclude<T>, "Register an unqualified component type");
		ComponentType id = getComponentType<T>();

		if (const auto existing = mComponentStorageType.find(id); existing != mComponentStorageType.end()) {
			if (existing->second != type) {
				throw std::logic_error("Conflicting component storage registration");
			}
			return;
		}

		if constexpr (!std::is_nothrow_move_constructible_v<T> || !std::is_nothrow_move_assignable_v<T> || !std::is_nothrow_destructible_v<T>) {
			if (mIndexedRecords && type == StorageType::Archetype) {
				throw std::invalid_argument("Shared-row archetype components require noexcept move and destruction");
			}
		}
		auto prototype = mIndexedRecords && type == StorageType::Archetype
			? std::make_shared<ComponentArray<T>>(MEMORY::StlAllocator<T, MEMORY::Category::Component> { indexedChunkResource() })
			: std::make_shared<ComponentArray<T>>();
		auto& pools = type == StorageType::Archetype ? mDefaultArraysArchetype : mSparseArrays;
		const auto registration = mComponentStorageType.emplace(id, type);
		try {
			pools.emplace(id, std::move(prototype));
		} catch (...) {
			mComponentStorageType.erase(registration.first);
			throw;
		}
	}

	RelationManager& getRelationManager() {
		return mRelationManager;
	}
	const RelationManager& getRelationManager() const {
		return mRelationManager;
	}
	EventManager& getEventManager() {
		return mEventManager;
	}
	const EventManager& getEventManager() const {
		return mEventManager;
	}

	template <typename T> UTILS::WeakPtr<T> getSingletonComponent() {
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		const auto type = getComponentType<T>();
		if (!mSingletonComponent.contains(type)) {
			return nullptr;
		}
		return mSingletonComponent.at(type)->template getWeak<T>();
	}

	template <typename T> UTILS::WeakPtr<T> addSingletonComponent() {
		requireStructuralChangesAllowed();
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		const auto type = getComponentType<T>();
		mSingletonComponent[type] = std::make_unique<T>();
		return getSingletonComponent<T>();
	}

	template <typename T> auto addComponent(Entity entity) {
		return addComponent(entity, T {});
	}

	template <typename T> auto addComponent(Entity entity, T&& newComponent) {
		using C = std::remove_cvref_t<T>;
		if constexpr (std::is_base_of_v<Component, C>) {
			addComponents(entity, std::forward<T>(newComponent));
			return getComponent<C>(entity);
		}
		else {
			const auto identity = addRvalueComponents<true>(entity, copyLvalues(std::forward<T>(newComponent)));
			if (identity.componentGeneration != 0) {
				SystemAccessScope::requireComponent(*this, identity.type, AccessMode::Read);
				return ComponentHandle<C> { EntityHandle { entity.getUniqueId(), mOwner, identity.entityGeneration }, identity.type,
					identity.componentGeneration };
			}
			return getComponentHandle<C>(entity);
		}
	}

	template <class... Components> void addComponents(Entity entity) {
		addComponents(entity, Components()...);
	}

	template <class... Components> void addComponents(Entity entity, Components&&... newComponent) {
		addRvalueComponents(entity, copyLvalues(std::forward<Components>(newComponent))...);
	}

private:
	template <class T> static decltype(auto) copyLvalues(T&& component) {
		if constexpr (std::is_lvalue_reference_v<T> || std::is_const_v<std::remove_reference_t<T>>) {
			return std::decay_t<T>(component);
		}
		else {
			return std::forward<T>(component);
		}
	}

	struct AddedIdentity {
		std::uint64_t entityGeneration = 0;
		std::uint64_t componentGeneration = 0;
		ComponentType type {};
	};

	template <bool CaptureIdentity = false, class... Components> auto addRvalueComponents(Entity entity, Components&&... newComponent) {
		requireStructuralChangesAllowed();
		const std::array<ComponentType, sizeof...(Components)> types { getComponentType<Components>()... };
		for (std::size_t i = 0; i < types.size(); ++i) {
			const auto preceding = std::span(types).first(i);
			if (std::ranges::find(preceding, types[i]) != preceding.end()) {
				throw std::logic_error("Component occurs more than once in an ECS add operation");
			}
			if (const auto pool = mSparseArrays.find(types[i]); pool != mSparseArrays.end() && pool->second->contains(entity)) {
				throw std::logic_error("Sparse component is already present");
			}
		}
		Record& record = mEntityRecords.at(entity);
		Archetype* nextArchetype = nullptr;
		const Archetype::Transition* plan = nullptr;
		if constexpr (sizeof...(Components) == 1) {
			const auto type = types.front();
			if ((ensureComponentRegistered<std::decay_t<Components>>(type), ...) == StorageType::Archetype) {
				plan = getSingleComponentTransition(record.archetype, type, true);
			}
			nextArchetype = plan ? plan->target : record.archetype;
		}
		else {
			auto mask = record.archetype->mMask;
			(addComponentToArchetypeMaskImpl<Components>(mask), ...);
			nextArchetype = getArchetype(std::move(mask)).get();
		}

		// Transfer existing archetype columns only. Each new value is inserted once below.
		AddedIdentity identity;
		if (mIndexedRecords) {
			const auto generation = addIndexedComponents(entity, record, nextArchetype, types, plan, newComponent...);
			if constexpr (CaptureIdentity) {
				static_assert(sizeof...(Components) == 1);
				identity = { record.generation, generation, types.front() };
			}
		}
		else {
			moveEntity<Components...>(entity, record.archetype, nextArchetype, record);
			(insertNewComponent(entity, record, std::forward<Components>(newComponent)), ...);
		}
		if constexpr (CaptureIdentity) {
			// A listener may remove or replace the new component before the handle is returned.
			if ((emitComponentEvent<OnAdd, std::decay_t<Components>>(entity), ...)) {
				return AddedIdentity {};
			}
			return identity;
		}
		else {
			(emitComponentEvent<OnAdd, std::decay_t<Components>>(entity), ...);
		}

		// auto signature = getSignature(entity);
		// signature.set(static_cast<int>(getComponentType<T>()), true);
		// setSignature(entity, newMask);
		// systemManager->entitySignatureChanged(entity, newMask);
	}

public:
	template <class... Components> void removeComponents(Entity entity) {
		requireStructuralChangesAllowed();
		// static_assert(sizeof...(Components) == 0, "Must contain non 0 components");
		(emitComponentEvent<OnRemove, Components>(entity), ...);
		Record& record = mEntityRecords.at(entity);
		auto archetype = record.archetype;
		Archetype* nextArchetype = nullptr;
		const Archetype::Transition* plan = nullptr;
		if constexpr (sizeof...(Components) == 1) {
			const auto type = (getComponentType<Components>(), ...);
			const auto storage = mComponentStorageType.find(type);
			if (storage != mComponentStorageType.end() && storage->second == StorageType::Archetype) {
				plan = getSingleComponentTransition(archetype, type, false);
			}
			nextArchetype = plan ? plan->target : archetype;
		}
		else {
			auto mask = archetype->mMask;
			(removeComponentFromArchetypeMaskImpl<Components>(mask), ...);
			nextArchetype = getArchetype(std::move(mask)).get();
		}

		if (mIndexedRecords) {
			prepareIndexedTransition(record, *nextArchetype);
			commitIndexedTransition(entity, record, nextArchetype, {}, plan);
		}
		else {
			moveEntity(entity, archetype, nextArchetype, record);
		}

		// Remove Sparse components
		(removeComponentFromSparseImpl<Components>(entity), ...);
	}

	template <typename T> UTILS::WeakPtr<T> getComponent(Entity entity) {
		static_assert(std::is_base_of_v<Component, T>, "Use tryGetComponent or getComponentHandle for value components");
		SystemAccessScope::requireComponent(*this, getComponentType<T>(), AccessMode::Write);
		auto* array = findArray<T>(entity);
		return array ? array->getDataPtr(entity) : nullptr;
	}

	// Borrowed until the next structural change. Use a handle for a retained reference.
	template <class T> [[nodiscard]] const T* tryGetComponent(Entity entity) const {
		SystemAccessScope::requireComponent(*this, getComponentType<T>(), AccessMode::Read);
		const auto* array = findArray<T>(entity);
		return array ? array->tryGetData(entity) : nullptr;
	}

	template <typename T> UTILS::WeakPtr<Component> getComponentBase(Entity entity) {
		auto component = getComponent<T>(entity);
		return component ? UTILS::WeakPtr<Component>(component.get()) : nullptr;
	}

	template <class T = Component> std::vector<UTILS::WeakPtr<T>> getComponents(Entity entity) {
		std::vector<UTILS::WeakPtr<T>> res;

		if (const auto* record = mEntityRecords.find(entity)) {
			const auto& archetype = record.archetype;

			for (auto& [id, componentArr] : archetype->mComponentArrays) {
				if (componentArr->contains(entity)) {
					SystemAccessScope::requireComponent(*this, id, AccessMode::Write);
					auto basePtr = componentArr->getComponentBasePtr(entity);
					if (basePtr) {
						if constexpr (std::is_same_v<T, Component> || std::is_same_v<T, IKIGAI::ECS::Component>) {
							res.push_back(std::static_pointer_cast<T>(basePtr));
						}
						else {
							auto casted = std::dynamic_pointer_cast<T>(basePtr);
							if (casted) {
								res.push_back(casted);
							}
						}
					}
				}
			}
		}

		for (auto& [id, sparseArr] : mSparseArrays) {
			if (sparseArr->contains(entity)) {
				SystemAccessScope::requireComponent(*this, id, AccessMode::Write);
				auto basePtr = sparseArr->getComponentBasePtr(entity);
				if (basePtr) {
					if constexpr (std::is_same_v<T, Component> || std::is_same_v<T, IKIGAI::ECS::Component>) {
						res.push_back(std::static_pointer_cast<T>(basePtr));
					}
					else {
						auto casted = std::dynamic_pointer_cast<T>(basePtr);
						if (casted) {
							res.push_back(casted);
						}
					}
				}
			}
		}

		return res;
	}

	template <typename T> bool checkComponent(Entity entity) const {
		SystemAccessScope::requireComponent(*this, getComponentType<T>(), AccessMode::Read);
		const auto* array = findArray<T>(entity);
		return array && array->contains(entity);
	}

private:
	ComponentArrayInterface* findArray(Entity entity, ComponentType type, std::uint64_t entityGeneration = 0) const;

	template <class T> ComponentArray<T>* findArray(Entity entity) const {
		static_assert(std::is_same_v<T, RawType<T>>, "Look up the unqualified component type");
		return static_cast<ComponentArray<T>*>(findArray(entity, getComponentType<T>()));
	}

public:
	std::shared_ptr<Archetype> getArchetype(Signature mask) {
		if (const auto existing = mMaskToArchetype.find(mask); existing != mMaskToArchetype.end()) {
			return existing->second;
		}
		requireStructuralChangesAllowed();
		auto newArchetype = std::make_shared<Archetype>();
		newArchetype->mMask = mask;
		if (mIndexedRecords) {
			newArchetype->mRows = std::make_shared<ArchetypeRows>();
			newArchetype->mRows->find = [records = std::weak_ptr<EntityRecords>(mIndexedRecords), owner = newArchetype.get()](Entity entity) {
				if (const auto locked = records.lock()) {
					const auto* record = locked->find(entity);
					if (record && record->archetype == owner) {
						return record->row;
					}
				}
				return ArchetypeRows::missing;
			};
		}
		for (const auto& [type, prototype] : mDefaultArraysArchetype) {
			if (mask[type.value()]) {
				auto column = prototype->createEmptyFromThis();
				if (newArchetype->mRows) {
					column->attachRows(newArchetype->mRows);
				}
				newArchetype->mComponentArrays.emplace(type, std::move(column));
			}
		}
		if (newArchetype->mRows) {
			newArchetype->mIndexedColumns.reserve(newArchetype->mComponentArrays.size());
			for (const auto& [type, column] : newArchetype->mComponentArrays) {
				newArchetype->mIndexedColumns.push_back(column.get());
			}
		}
		mMaskToArchetype.emplace(std::move(mask), newArchetype);
		return newArchetype;
	}

private:
	template <bool Generated> void registerEntityImpl(Entity entity) {
		if constexpr (!Generated) {
			if (!entity.getUniqueId().valid()) {
				throw std::invalid_argument("Cannot register an invalid entity");
			}
		}
		requireStructuralChangesAllowed();
		if (mNextEntityGeneration == 0) {
			if constexpr (!Generated) {
				if (mEntityRecords.find(entity)) {
					throw std::logic_error("Entity is already registered in this ECS world");
				}
			}
			throw std::overflow_error("ECS entity generation exhausted");
		}
		if (!mEmptyArchetype) {
			mEmptyArchetype = getArchetype(Signature {}).get();
		}
		if constexpr (Generated) {
			mEntityRecords.insertGenerated(entity, mEmptyArchetype, mNextEntityGeneration);
		}
		else if (!mEntityRecords.insertSupplied(entity, mEmptyArchetype, mNextEntityGeneration)) {
			throw std::logic_error("Entity is already registered in this ECS world");
		}
		++mNextEntityGeneration;
	}

public:
	void registerEntity(Entity entity) {
		registerEntityImpl<false>(entity);
	}

	[[nodiscard]] EntityHandle getEntityHandle(Entity entity) const {
		const auto* record = mEntityRecords.find(entity);
		if (!record) {
			return {};
		}
		return { entity.getUniqueId(), mOwner, record->generation };
	}

	[[nodiscard]] std::optional<Entity> resolveEntity(EntityHandle handle) const {
		if (handle.owner != mOwner || !handle.id.valid() || handle.generation == 0) {
			return std::nullopt;
		}
		const Entity entity(handle.id);
		const auto* record = mEntityRecords.find(entity);
		if (!record || record->generation != handle.generation) {
			return std::nullopt;
		}
		return entity;
	}

	template <class T> [[nodiscard]] ComponentHandle<T> getComponentHandle(Entity entity) const {
		SystemAccessScope::requireComponent(*this, getComponentType<T>(), AccessMode::Read);
		if (mIndexedRecords) {
			const auto* record = mEntityRecords.find(entity);
			if (!record) {
				return {};
			}
			const auto type = getComponentType<T>();
			const auto& columns = record->archetype->mComponentArrays;
			const auto column = columns.find(type);
			const auto sparse = column == columns.end() ? mSparseArrays.find(type) : mSparseArrays.end();
			const auto generation = column != columns.end() ? static_cast<const ComponentArray<T>&>(*column->second).mGenerations[record->row]
				: sparse != mSparseArrays.end()				? static_cast<const ComponentArray<T>&>(*sparse->second).getGeneration(entity)
															: 0;
			return generation == 0 ? ComponentHandle<T> {}
								   : ComponentHandle<T> { EntityHandle { entity.getUniqueId(), mOwner, record->generation }, type, generation };
		}
		const auto* array = findArray<T>(entity);
		const auto generation = array ? array->getGeneration(entity) : 0;
		return generation == 0 ? ComponentHandle<T> {} : ComponentHandle<T> { getEntityHandle(entity), getComponentType<T>(), generation };
	}

	template <class T> [[nodiscard]] ComponentHandle<Component> getComponentBaseHandle(Entity entity) const {
		static_assert(std::is_base_of_v<Component, T>);
		const auto handle = getComponentHandle<T>(entity);
		return { handle.mOwner, handle.mType, handle.mGeneration };
	}

	// Resolving gives a borrowed pointer. Resolve again after structural changes.
	template <class T> [[nodiscard]] const T* resolveComponent(const ComponentHandle<T>& handle) const {
		const auto& owner = handle.mOwner;
		if (owner.owner != mOwner || !owner.id.valid() || owner.generation == 0) {
			return nullptr;
		}
		SystemAccessScope::requireComponent(*this, handle.mType, AccessMode::Read);
		const Entity entity(owner.id);
		const auto* array = findArray(entity, handle.mType, owner.generation);
		if constexpr (std::is_same_v<T, Component>) {
			return array ? array->tryGetBaseData(entity, handle.mGeneration) : nullptr;
		}
		else {
			return array ? static_cast<const ComponentArray<T>*>(array)->tryGetData(entity, handle.mGeneration) : nullptr;
		}
	}

	template <class T> [[nodiscard]] T* resolveComponent(const ComponentHandle<T>& handle) {
		if (handle.mGeneration != 0) {
			SystemAccessScope::requireComponent(*this, handle.mType, AccessMode::Write);
		}
		return const_cast<T*>(std::as_const(*this).resolveComponent(handle));
	}

	void destroyEntity(Entity entity) {
		requireStructuralChangesAllowed();
		const auto location = mEntityRecords.locateForErase(entity);
		const auto* record = location.record;
		if (!record) {
			return;
		}
		if (record->archetype == mEmptyArchetype && mSparseArrays.empty()) {
			mRelationManager.onEntityDestroyed(entity);
			mEntityRecords.eraseLocated(entity, location);
			return;
		}
		auto archetype = record->archetype;
		const auto row = record->row;
		for (auto& [type, array] : mSparseArrays) {
			array->entityDestroyed(entity);
		}
		if (archetype->mRows) {
			if (row != ArchetypeRows::missing) {
				eraseIndexedEntity(*archetype, row);
			}
		}
		else {
			int columnSize = -1;
			for (auto& [type, array] : archetype->mComponentArrays) {
				if (columnSize < 0) {
					columnSize = array->getSize();
				}
				else if (array->getSize() != columnSize) {
					throw std::logic_error("Archetype column sizes diverged");
				}
			}
			for (auto& [type, array] : archetype->mComponentArrays) {
				array->entityDestroyed(entity);
			}
		}
		mRelationManager.onEntityDestroyed(entity);
		mEntityRecords.erase(entity);
	}

	// Visit order follows the selected storage and can change after structural edits.
	template <class... Components, class Func> void forEach(Func&& func) {
		const QueryScope query(mActiveQueries);
		static_assert(sizeof...(Components) > 0, "forEach requires components");
		static_assert((!IsExclude<RawType<Components>> && ...), "Exclude is not supported by forEach");
		constexpr auto count = sizeof...(Components);
		const std::array types { getComponentType<Components>()... };
		constexpr std::array accessModes { (AccessTraits<Components>::ReadOnly ? AccessMode::Read : AccessMode::Write)... };
		for (std::size_t i = 0; i < count; ++i) {
			SystemAccessScope::requireComponent(*this, types[i], accessModes[i]);
		}
		std::array<ComponentArrayInterface*, count> arrays {};
		std::array<bool, count> rowAligned {};
		ComponentArrayInterface* smallestSparse = nullptr;
		std::size_t firstArchetype = count;
		std::size_t archetypeTerms = 0;
		for (std::size_t i = 0; i < count; ++i) {
			const auto storage = mComponentStorageType.find(types[i]);
			if (storage == mComponentStorageType.end()) {
				return;
			}
			if (storage->second == StorageType::Archetype) {
				rowAligned[i] = true;
				++archetypeTerms;
				if (firstArchetype == count) {
					firstArchetype = i;
				}
			}
			else {
				arrays[i] = mSparseArrays.at(types[i]).get();
				if (!smallestSparse || arrays[i]->getSize() < smallestSparse->getSize()) {
					smallestSparse = arrays[i];
				}
			}
		}
		if (firstArchetype == count) {
			visitRows<Components...>(arrays, rowAligned, *smallestSparse, func);
			return;
		}
		const auto matches = [&](const Signature& signature) {
			for (std::size_t i = firstArchetype; i < count; ++i) {
				if (rowAligned[i] && !signature.test(types[i].value())) {
					return false;
				}
			}
			return true;
		};
		const auto bindColumns = [&](const Archetype& archetype) {
			for (std::size_t i = firstArchetype; i < count; ++i) {
				if (rowAligned[i]) {
					arrays[i] = archetype.mComponentArrays.at(types[i]).get();
				}
			}
		};
		if (smallestSparse) {
			const auto sparseRows = smallestSparse->getEntities();
			if (sparseRows.empty()) {
				return;
			}
			std::uint64_t archetypeRows = 0;
			std::size_t matchingArchetypes = 0;
			const Archetype* matchingArchetype = nullptr;
			for (const auto& [signature, archetype] : mMaskToArchetype) {
				if (matches(signature)) {
					const auto rows = archetype->size();
					if (rows != 0) {
						archetypeRows += rows;
						++matchingArchetypes;
						matchingArchetype = archetype.get();
					}
				}
			}
			if (matchingArchetypes == 0) {
				return;
			}

			// Across tables, budget a record lookup and a column lookup per archetype term.
			const auto probesPerSparseRow = mIndexedRecords ? (matchingArchetypes == 1 ? count - archetypeTerms : count)
															: (matchingArchetypes == 1 ? count - 1 : count + archetypeTerms);
			if (static_cast<std::uint64_t>(sparseRows.size()) * probesPerSparseRow < archetypeRows * (count - archetypeTerms)) {
				constexpr std::array<bool, count> sparseAligned {};
				if (matchingArchetypes == 1 && !mIndexedRecords) {
					bindColumns(*matchingArchetype);
					visitRows<Components...>(arrays, sparseAligned, *smallestSparse, func);
					return;
				}
				if constexpr (count == 2) {
					if (matchingArchetypes == 1) {
						bindColumns(*matchingArchetype);
						if (firstArchetype == 0) {
							visitIndexedPair<true, Components...>(arrays, *matchingArchetype, func);
						}
						else {
							visitIndexedPair<false, Components...>(arrays, *matchingArchetype, func);
						}
						return;
					}
				}

				const Archetype* previousArchetype = nullptr;
				bool matchesPrevious = false;
				EntityRecords::PageCursor cursor;
				for (std::size_t row = 0; row < sparseRows.size(); ++row) {
					const Entity entity = sparseRows[row];
					const auto* record = mEntityRecords.find(entity, cursor);
					if (!record) {
						continue;
					}
					const auto* archetype = record->archetype;
					if (archetype != previousArchetype) {
						previousArchetype = archetype;
						matchesPrevious = matches(archetype->mMask);
						if (matchesPrevious) {
							bindColumns(*archetype);
						}
					}
					if (matchesPrevious) {
						visitJoinedRow<Components...>(arrays, mIndexedRecords ? rowAligned : sparseAligned, *smallestSparse, row, entity, func,
							std::index_sequence_for<Components...> {}, record->row);
					}
				}
				return;
			}
		}
		for (const auto& [signature, archetype] : mMaskToArchetype) {
			if (!matches(signature)) {
				continue;
			}
			bindColumns(*archetype);
			if (smallestSparse) {
				visitRows<Components...>(arrays, rowAligned, *arrays[firstArchetype], func);
			}
			else {
				visitDense<Components...>(arrays, func, std::index_sequence_for<Components...> {});
			}
		}
	}

	template <class T> ComponentArray<T>& getComponentsArray() {
		static_assert(std::is_same_v<T, RawType<T>>, "Get the pool by its unqualified component type");
		auto type = getComponentType<T>();
		SystemAccessScope::requireComponent(*this, type, AccessMode::Write);
		if (mSparseArrays.contains(type)) {
			return *(std::static_pointer_cast<ComponentArray<T>>(mSparseArrays.at(type)));
		}
		throw std::runtime_error("Requested component does not have a sparse pool");
	}

	Signature getSignature(Entity entity) {
#ifdef __DEBUG__
		if (!mSignatures.contains(entity)) {
			throw;
		}
#endif
		return mSignatures[entity];
	}
};
}
