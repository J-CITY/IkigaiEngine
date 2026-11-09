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
		return mComponentArrays.empty() ? 0 : mComponentArrays.begin()->second->getSize();
	}
};

struct Record {
	std::unordered_map<ComponentType, std::unordered_map<TagType, std::any>> mTagsMap;
	std::shared_ptr<Archetype> archetype;
	std::uint64_t generation = 0;
};

class ComponentManager {
public:
	ComponentManager() { };

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
	std::unordered_map<Entity, Record> mEntityRecords;
	std::unordered_map<Signature, std::shared_ptr<Archetype>> mMaskToArchetype;
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
	template <typename T> void ensureComponentRegistered(ComponentType id) {
		if (mComponentStorageType.contains(id)) {
			return;
		}

		// Default storage is Archetype. Sparse is opt-in via registerComponent.
		registerComponent<T>(StorageType::Archetype);
	}

	template <typename T, typename... Args> void addComponentToArchetypeImpl(Entity entity, Archetype& to, T&& component) {
		using C = std::decay_t<T>;
		const auto cname = getComponentType<C>();

		// We assume this is only called for Archetype components
		// But careful validation or design is needed.
		// Ideally addComponents logic separates them.

		if (to.mComponentArrays.contains(cname)) {
			std::static_pointer_cast<ComponentArray<C>>(to.mComponentArrays.at(cname))->insert(entity, std::forward<T>(component));
		}
	}

	template <typename T> void addComponentToArchetypeMaskImpl(Signature& mask) {
		using C = std::decay_t<T>;
		ComponentType id = getComponentType<C>();
		ensureComponentRegistered<C>(id);
		if (mComponentStorageType[id] == StorageType::Archetype) {
			mask.set(static_cast<size_t>(id), true);
		}
	}

	template <typename... Component> void addComponentsToArchetypeMask(Signature& mask) {
		(addComponentToArchetypeMaskImpl<Component>(mask), ...);
	}

	template <typename T> void addComponentToSparseImpl(Entity entity, T&& component) {
		using C = std::decay_t<T>;
		ComponentType id = getComponentType<C>();
		ensureComponentRegistered<C>(id);
		if (mComponentStorageType[id] == StorageType::Sparse) {
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
	template <class... NewComponents> void moveEntity(Entity entity, std::shared_ptr<Archetype> from, std::shared_ptr<Archetype> to, Record& record) {
		const bool replacesExisting = ((from->mComponentArrays.contains(getComponentType<std::decay_t<NewComponents>>())) || ...);
		if (from.get() == to.get() && !replacesExisting) {
			record.archetype = to;
			return;
		}

		for (auto& [ctype, fromArr] : from->mComponentArrays) {
			if (!fromArr->contains(entity)) {
				continue;
			}
			const bool inPack = ((getComponentType<std::decay_t<NewComponents>>() == ctype) || ...);
			const bool keep = to->mComponentArrays.contains(ctype) && !inPack;
			if (!keep) {
				fromArr->entityDestroyed(entity);
				continue;
			}
			if (from.get() != to.get()) {
				fromArr->transferEntityTo(entity, *to->mComponentArrays.at(ctype));
			}
			else if (replacesExisting) {
				// Same table: replay remove+append so a replaced column stays index-aligned.
				fromArr->transferEntityTo(entity, *fromArr);
			}
		}

		if (from.get() != to.get()) {
			for (auto& [ctype, fromArr] : from->mComponentArrays) {
				if (fromArr->contains(entity)) {
					throw std::logic_error("Entity remained in source archetype");
				}
			}
		}
		record.archetype = to;
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
	static void visitRows(const std::array<ComponentArrayInterface*, sizeof...(Components)>& arrays,
		const std::array<bool, sizeof...(Components)>& rowAligned, ComponentArrayInterface& leading, Func& func, std::index_sequence<Indices...>) {
		const auto count = static_cast<std::size_t>(leading.getSize());
		for (std::size_t row = 0; row < count; ++row) {
			const Entity entity = leading.getEntity(row);
			auto pointers = std::tuple { ([&] {
				auto& array = *static_cast<ComponentArray<RawType<Components>>*>(arrays[Indices]);
				return rowAligned[Indices] || arrays[Indices] == &leading ? &array.at(row) : array.tryGetData(entity);
			}())... };
			if (((std::get<Indices>(pointers) != nullptr) && ...)) {
				std::invoke(func, entity, static_cast<QueryReference<Components>>(*std::get<Indices>(pointers))...);
			}
		}
	}

	template <typename T> void emitOnAdd(Entity entity) {
		auto* array = findArray<T>(entity);
		if (auto* component = array ? array->tryGetData(entity) : nullptr) {
			mEventManager.emit(OnAdd<T> { entity, component });
		}
	}

	template <typename T> void emitOnRemove(Entity entity) {
		auto* array = findArray<T>(entity);
		if (auto* component = array ? array->tryGetData(entity) : nullptr) {
			mEventManager.emit(OnRemove<T> { entity, component });
		}
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
		// std::cout << typeid(T).name() << " - " << (int)componentId << "\n";
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

		mComponentStorageType[id] = type;
		if (type == StorageType::Archetype) {
			mDefaultArraysArchetype.insert({ id, std::make_shared<ComponentArray<T>>() });
		}
		else {
			mSparseArrays.insert({ id, std::make_shared<ComponentArray<T>>() });
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
		addComponents(entity, std::forward<T>(newComponent));
		if constexpr (std::is_base_of_v<Component, C>) {
			return getComponent<C>(entity);
		}
		else {
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

	template <class... Components> void addRvalueComponents(Entity entity, Components&&... newComponent) {
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
		auto newMask = record.archetype->mMask;
		// set all components to mask (Only Archetype ones)
		(addComponentToArchetypeMaskImpl<Components>(newMask), ...);
		auto nextArchetype = getArchetype(newMask);

		// Transfer existing archetype columns only. Each new value is inserted once below.
		moveEntity<Components...>(entity, record.archetype, nextArchetype, record);
		(insertNewComponent(entity, record, std::forward<Components>(newComponent)), ...);
		(emitOnAdd<std::decay_t<Components>>(entity), ...);

		// auto signature = getSignature(entity);
		// signature.set(static_cast<int>(getComponentType<T>()), true);
		// setSignature(entity, newMask);
		// systemManager->entitySignatureChanged(entity, newMask);
	}

public:
	template <class... Components> void removeComponents(Entity entity) {
		requireStructuralChangesAllowed();
		// static_assert(sizeof...(Components) == 0, "Must contain non 0 components");
		(emitOnRemove<Components>(entity), ...);
		Record& record = mEntityRecords.at(entity);
		auto archetype = record.archetype;
		auto newMask = archetype->mMask;
		// set all components to mask (Only Archetype ones)
		(removeComponentFromArchetypeMaskImpl<Components>(newMask), ...);
		auto nextArchetype = getArchetype(newMask);

		moveEntity(entity, archetype, nextArchetype, record);

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

		if (mEntityRecords.contains(entity)) {
			const Record& record = mEntityRecords.at(entity);
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
		for (const auto& [type, prototype] : mDefaultArraysArchetype) {
			if (mask[type.value()]) {
				newArchetype->mComponentArrays.emplace(type, prototype->createEmptyFromThis());
			}
		}
		mMaskToArchetype.emplace(std::move(mask), newArchetype);
		return newArchetype;
	}

	void registerEntity(Entity entity) {
		requireStructuralChangesAllowed();
		if (mEntityRecords.contains(entity)) {
			throw std::logic_error("Entity is already registered in this ECS world");
		}
		if (mNextEntityGeneration == 0) {
			throw std::overflow_error("ECS entity generation exhausted");
		}
		mEntityRecords.emplace(entity, Record { {}, getArchetype(Signature()), mNextEntityGeneration++ });
	}

	[[nodiscard]] EntityHandle getEntityHandle(Entity entity) const {
		const auto record = mEntityRecords.find(entity);
		if (record == mEntityRecords.end()) {
			return {};
		}
		return { entity.getUniqueId(), mOwner, record->second.generation };
	}

	[[nodiscard]] std::optional<Entity> resolveEntity(EntityHandle handle) const {
		if (handle.owner != mOwner || !handle.id.valid() || handle.generation == 0) {
			return std::nullopt;
		}
		const Entity entity(handle.id);
		const auto record = mEntityRecords.find(entity);
		if (record == mEntityRecords.end() || record->second.generation != handle.generation) {
			return std::nullopt;
		}
		return entity;
	}

	template <class T> [[nodiscard]] ComponentHandle<T> getComponentHandle(Entity entity) const {
		SystemAccessScope::requireComponent(*this, getComponentType<T>(), AccessMode::Read);
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
		const auto it = mEntityRecords.find(entity);
		if (it == mEntityRecords.end()) {
			return;
		}
		auto archetype = it->second.archetype;
		for (auto& [type, array] : mSparseArrays) {
			array->entityDestroyed(entity);
		}
		if (archetype) {
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

	template <class... Components, class Func> void forEach(Func&& func) {
		const QueryScope query(mActiveQueries);
		static_assert(sizeof...(Components) > 0, "forEach requires components");
		static_assert((!IsExclude<RawType<Components>> && ...), "Exclude is not supported by forEach");
		constexpr auto count = sizeof...(Components);
		const std::array types { getComponentType<Components>()... };
		(SystemAccessScope::requireComponent(
			 *this, getComponentType<Components>(), AccessTraits<Components>::ReadOnly ? AccessMode::Read : AccessMode::Write),
			...);
		std::array<ComponentArrayInterface*, count> arrays {};
		std::array<bool, count> rowAligned {};
		Signature mask;
		ComponentArrayInterface* smallestSparse = nullptr;
		std::size_t firstArchetype = count;
		for (std::size_t i = 0; i < count; ++i) {
			const auto storage = mComponentStorageType.find(types[i]);
			if (storage == mComponentStorageType.end()) {
				return;
			}
			if (storage->second == StorageType::Archetype) {
				mask.set(types[i].value());
				rowAligned[i] = true;
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
			visitRows<Components...>(arrays, rowAligned, *smallestSparse, func, std::index_sequence_for<Components...> {});
			return;
		}
		for (const auto& [signature, archetype] : mMaskToArchetype) {
			if (!signature.contains(mask)) {
				continue;
			}
			for (std::size_t i = 0; i < count; ++i) {
				if (rowAligned[i]) {
					arrays[i] = archetype->mComponentArrays.at(types[i]).get();
				}
			}
			visitRows<Components...>(arrays, rowAligned, *arrays[firstArchetype], func, std::index_sequence_for<Components...> {});
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
