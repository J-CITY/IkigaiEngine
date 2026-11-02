#pragma once
#include "access.h"
#include "events.h"
#include "relationManager.h"
#include "signature.h"
#include <memory>
#include <string>
#include <unordered_set>

#include "componentArray.h"
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

using ComponentType = Id<ComponentManager>;
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
};

class ComponentManager {
public:
	ComponentManager() { };

private:
	std::unordered_map<Entity, Signature> mSignatures;

	ComponentType mNextComponentType = IdGenerator<ComponentManager>::generateId();

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
		mComponentStorageType[id] = StorageType::Archetype;
		mDefaultArraysArchetype.insert({ id, std::make_shared<ComponentArray<T>>() });
	}

	// Called by getComponentType static initialization
	template <typename T> ComponentType registerNewComponentId() {
		ComponentType id = mNextComponentType;
		mNextComponentType = IdGenerator<ComponentManager>::generateId();
		return id;
	}

	template <typename T, typename... Args> void addComponentToArchetypeImpl(Entity entity, Archetype& to, T&& component) {
		using C = std::decay_t<T>;
		static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
		const auto cname = getComponentType<C>();

		// We assume this is only called for Archetype components
		// But careful validation or design is needed.
		// Ideally addComponents logic separates them.

		if (to.mComponentArrays.contains(cname)) {
			std::static_pointer_cast<ComponentArray<C>>(to.mComponentArrays.at(cname))->insert(entity, std::move(component));
		}
	}

	template <typename T> void addComponentToArchetypeMaskImpl(Signature& mask) {
		using C = std::decay_t<T>;
		static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
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
		static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
		ComponentType id = getComponentType<C>();
		ensureComponentRegistered<C>(id);
		if (mComponentStorageType[id] == StorageType::Sparse) {
			std::static_pointer_cast<ComponentArray<C>>(mSparseArrays[id])->insert(entity, std::move(component));
		}
	}

	template <typename T> void removeComponentFromArchetypeMaskImpl(Signature& mask) {
		using C = std::decay_t<T>;
		static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
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
		static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
		ComponentType id = getComponentType<C>();
		if (mComponentStorageType.contains(id) && mComponentStorageType[id] == StorageType::Sparse) {
			std::static_pointer_cast<ComponentArray<C>>(mSparseArrays[id])->removeData(entity);
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
				ASSERT_IF(!fromArr->contains(entity), "Entity ghosted in source archetype");
			}
		}
		record.archetype = to;
	}

	template <typename T> void insertNewComponent(Entity entity, Record& record, T&& component) {
		using C = std::decay_t<T>;
		static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
		const ComponentType id = getComponentType<C>();
		ensureComponentRegistered<C>(id);
		if (mComponentStorageType.at(id) == StorageType::Sparse) {
			addComponentToSparseImpl(entity, std::forward<T>(component));
			return;
		}
		addComponentToArchetypeImpl(entity, *record.archetype, std::forward<T>(component));
	}

	template <typename T> RawType<T>& forEachComponentsImpl(Archetype& archetype, size_t i, [[maybe_unused]] Entity entity) {
		using U = RawType<T>;
		static_assert(std::is_base_of_v<Component, U>, "Must inherit from class Component");
		ComponentType id = getComponentType<U>();
		const auto storage = mComponentStorageType.find(id);
		if (storage != mComponentStorageType.end() && storage->second == StorageType::Archetype) {
			return archetype.getComponents<U>(id)->at(i);
		}
		return std::static_pointer_cast<ComponentArray<U>>(mSparseArrays.at(id))->getData(entity);
	}

	template <class... Components, class F> void forEachComponents(Archetype& archetype, size_t i, Entity entity, F&& func) {
		func(entity, forEachComponentsImpl<Components>(archetype, i, entity)...);
	}

	template <typename T> void emitOnAdd(Entity entity) {
		auto c = getComponent<T>(entity);
		if (c) {
			mEventManager.emit(OnAdd<T> { entity, c.get() });
		}
	}

	template <typename T> void emitOnRemove(Entity entity) {
		auto c = getComponent<T>(entity);
		if (c) {
			mEventManager.emit(OnRemove<T> { entity, c.get() });
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
		static_assert(std::is_base_of_v<Component, First>, "Must inherit from class Component");
		ComponentType id = getComponentType<First>();

		if (!mComponentStorageType.contains(id) || mComponentStorageType[id] != StorageType::Sparse) {
			return;
		}

		auto& sparseArrInterface = mSparseArrays[id];
		auto sparseArr = std::static_pointer_cast<ComponentArray<First>>(sparseArrInterface);

		for (size_t i = 0; i < sparseArr->getSize(); ++i) {
			Entity entity = sparseArr->getEntity(i);
			First& firstComp = sparseArr->at(i);
			func(entity, firstComp);
		}
	}

	template <typename T> ComponentType getComponentTypeImpl() {
		static ComponentType componentId = registerNewComponentId<T>();
		// std::cout << typeid(T).name() << " - " << (int)componentId << "\n";
		return componentId;
	}

	template <typename T> ComponentType getComponentType() {
		return getComponentTypeImpl<RawType<T>>();
	}

	template <typename T> void registerComponent(StorageType type = StorageType::Archetype) {
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		ComponentType id = getComponentType<T>();

		if (mComponentStorageType.contains(id)) {
			// Already registered. Check if consistent? or throw?
			// For now allow re-registration if same type? Or ignore.
			// If we want to change type runtime, that's complex (need data migration).
			// Assume initialization time.
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
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		const auto type = getComponentType<T>();
		mSingletonComponent[type] = std::make_unique<T>();
		return getSingletonComponent<T>();
	}

	template <typename T> UTILS::WeakPtr<T> addComponent(Entity entity) {
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		addComponents<T>(entity, T());
		return getComponent<T>(entity);
	}

	template <typename T> UTILS::WeakPtr<T> addComponent(Entity entity, T&& newComponent) {
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		addComponents<T>(entity, std::move(newComponent));
		return getComponent<T>(entity);
	}

	template <class... Components> void addComponents(Entity entity) {
		addComponents(entity, (Components(), ...));
	}

	template <class... Components> void addComponents(Entity entity, Components... newComponent) {
		Record& record = mEntityRecords.at(entity);
		auto newMask = record.archetype->mMask;
		// set all components to mask (Only Archetype ones)
		(addComponentToArchetypeMaskImpl<Components>(newMask), ...);
		auto nextArchetype = getArchetype(newMask);

		// Transfer existing archetype columns only. Each new value is inserted once below.
		moveEntity<Components...>(entity, record.archetype, nextArchetype, record);
		(insertNewComponent(entity, record, std::move(newComponent)), ...);
		(emitOnAdd<Components>(entity), ...);

		// auto signature = getSignature(entity);
		// signature.set(static_cast<int>(getComponentType<T>()), true);
		// setSignature(entity, newMask);
		// systemManager->entitySignatureChanged(entity, newMask);
	}

	template <class... Components> void removeComponents(Entity entity) {
		// static_assert(sizeof...(Components) == 0, "Must contain non 0 components");
		(emitOnRemove<Components>(entity), ...);
		Record& record = mEntityRecords[entity];
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
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		ComponentType id = getComponentType<T>();

		// Check Archetype first
		const auto recordIt = mEntityRecords.find(entity);
		if (recordIt == mEntityRecords.end()) {
			return nullptr;
		}
		const Record& record = recordIt->second;
		const auto& archetype = record.archetype;

		auto it = archetype->mComponentArrays.find(id);
		if (it != archetype->mComponentArrays.end()) {
			auto& componentArr = it->second;
			return std::static_pointer_cast<ComponentArray<T>>(componentArr)->getDataPtr(entity);
		}

		// Check Sparse
		if (mSparseArrays.contains(id)) {
			return std::static_pointer_cast<ComponentArray<T>>(mSparseArrays[id])->getDataPtr(entity);
		}

		return nullptr;
	}

	template <typename T> UTILS::WeakPtr<Component> getComponentBase(Entity entity) {
		return getComponentBase<T>(entity);
	}

	template <class T = Component> std::vector<UTILS::WeakPtr<T>> getComponents(Entity entity) {
		std::vector<UTILS::WeakPtr<T>> res;

		if (mEntityRecords.contains(entity)) {
			const Record& record = mEntityRecords.at(entity);
			const auto& archetype = record.archetype;

			for (auto& [id, componentArr] : archetype->mComponentArrays) {
				if (componentArr->contains(entity)) {
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

	template <typename T> bool checkComponent(Entity entity) {
		static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
		ComponentType id = getComponentType<T>();

		// Archetype check
		const auto recordIt = mEntityRecords.find(entity);
		if (recordIt == mEntityRecords.end()) {
			return false;
		}
		const Record& record = recordIt->second;
		const auto& archetype = record.archetype;
		if (archetype) {
			const auto column = archetype->mComponentArrays.find(id);
			if (column != archetype->mComponentArrays.end()) {
				return column->second->contains(entity);
			}
		}

		if (mSparseArrays.contains(id)) {
			return mSparseArrays.at(id)->contains(entity);
		}
		return false;
	}

	std::shared_ptr<Archetype> getArchetype(Signature mask) {
		if (!mMaskToArchetype.contains(mask)) {
			mMaskToArchetype[mask] = std::make_shared<Archetype>();
			auto& newArchetype = mMaskToArchetype[mask];
			newArchetype->mMask = mask;
			for (int id = 0; id < static_cast<int>(mNextComponentType); ++id) {
				if (mask[static_cast<size_t>(id)]) {
					auto type = ComponentType(id);
					newArchetype->mComponentArrays[type] = mDefaultArraysArchetype[type]->createEmptyFromThis();
				}
			}
		}
		return mMaskToArchetype[mask];
	}

	void registerEntity(Entity entity) {
		mEntityRecords[entity] = Record { {}, getArchetype(Signature()) };
	}

	void destroyEntity(Entity entity) {
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
				else {
					ASSERT_IF(array->getSize() == columnSize, "Archetype column sizes diverged");
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
		static_assert(sizeof...(Components) > 0, "forEach requires components");
		Signature mask;
		(addComponentToArchetypeMaskImpl<Components>(mask), ...);
		using FirstComponent = std::tuple_element_t<0, std::tuple<std::decay_t<Components>...>>;
		const ComponentType canonicalType = getComponentType<FirstComponent>();

		for (auto& [m, a] : mMaskToArchetype) {
			if ((mask & m) != mask) {
				continue;
			}
			const auto columnIt = a->mComponentArrays.find(canonicalType);
			if (columnIt == a->mComponentArrays.end()) {
				continue;
			}
			auto& column = columnIt->second;
			for (const auto& [ctype, arr] : a->mComponentArrays) {
				ASSERT_IF(arr->getSize() == column->getSize(), "Archetype column sizes diverged");
			}
			for (size_t i = 0; i < static_cast<size_t>(column->getSize()); ++i) {
				Entity entity = column->getEntity(i);
				if ((checkComponent<Components>(entity) && ...)) {
					forEachComponents<Components...>(*a, i, entity, func);
				}
			}
		}
	}

	template <class T> ComponentArray<T>& getComponentsArray() {
		auto type = getComponentType<T>();
		if (mSparseArrays.contains(type)) {
			return *(std::static_pointer_cast<ComponentArray<T>>(mSparseArrays.at(type)));
		}
		ASSERT("Array is not exist");
		// LOG_ERROR("Array does not exist");
		static auto def = std::make_shared<ComponentArray<T>>();
		return *def;
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
