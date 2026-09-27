#pragma once
#include "signature.h"
#include "relationManager.h"
#include "events.h"
#include "access.h"
#include <memory>
#include <string>
#include <unordered_set>

#include "componentArray.h"
#include "entityManager.h"
#include "utilsModule/idGenerator.h"
#include "utilsModule/singleton.h"

#include "utilsModule/log/loggerDefine.h"
#include <iostream>

namespace IKIGAI::ECS2 {
	//TODO: use faster std container (EA for example)
	//TODO: use allocators for containers

	class ComponentManager;

	using ComponentType = Id<ComponentManager>;
	using TagType = ComponentType;

	enum class StorageType {
		Archetype,
		Sparse
	};

	class /*alignas(32)*/ Component: public UTILS::ControlBlockHandler
	{
	public:
		Component() = default;
		virtual ~Component() override = default;

		template<class T>
		UTILS::WeakPtr<T> getWeak() {
			return UTILS::WeakPtr<T>(*this);
		}
	};

	class Archetype {
	public:
		Signature mMask;
		std::unordered_map<ComponentType, std::shared_ptr<ComponentArrayInterface>> mComponentArrays;

		template<class T>
		std::shared_ptr<ComponentArray<T>> getComponents(ComponentType type) {
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
		ComponentManager() {};

	private:
		std::unordered_map<Entity, Signature> mSignatures;

		ComponentType mNextComponentType = IdGenerator<ComponentManager>::generateId();

		//Archetype structs
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
		template<typename T>
		void ensureComponentRegistered(ComponentType id) {
			if (mComponentStorageType.contains(id)) return;

			// Default to Archetype
			mComponentStorageType[id] = StorageType::Archetype;
			mDefaultArraysArchetype.insert({ id, std::make_shared<ComponentArray<T>>() });
		}

		// Called by getComponentType static initialization
		template<typename T>
		ComponentType registerNewComponentId() {
			ComponentType id = mNextComponentType;
			mNextComponentType = IdGenerator<ComponentManager>::generateId();
			return id;
		}

		template<typename T, typename... Args>
		void addComponentToArchetypeImpl(Entity entity, Archetype& to, T&& component) {
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

		template<typename T>
		void addComponentToArchetypeMaskImpl(Signature& mask) {
			using C = std::decay_t<T>;
			static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
			ComponentType id = getComponentType<C>();
			ensureComponentRegistered<C>(id);
			if (mComponentStorageType[id] == StorageType::Archetype) {
				mask.set(static_cast<size_t>(id), true);
			}
		}

		template<typename... Component>
		void addComponentsToArchetypeMask(Signature& mask) {
			(addComponentToArchetypeMaskImpl<Component>(mask), ...);
		}

		template<typename T>
		void addComponentToSparseImpl(Entity entity, T&& component) {
			using C = std::decay_t<T>;
			static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
			ComponentType id = getComponentType<C>();
			ensureComponentRegistered<C>(id);
			if (mComponentStorageType[id] == StorageType::Sparse) {
				std::static_pointer_cast<ComponentArray<C>>(mSparseArrays[id])->insert(entity, std::move(component));
			}
		}

		template<typename T>
		void removeComponentFromArchetypeMaskImpl(Signature& mask) {
			using C = std::decay_t<T>;
			static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
			ComponentType id = getComponentType<C>();
			if (mComponentStorageType.contains(id) && mComponentStorageType[id] == StorageType::Archetype) {
				mask.set(static_cast<size_t>(id), false);
			}
		}

		template<typename... Component>
		void removeComponentsFromArchetypeMask(Signature& mask) {
			(removeComponentFromArchetypeMaskImpl<Component>(mask), ...);
		}

		template<typename T>
		void removeComponentFromSparseImpl(Entity entity) {
			using C = std::decay_t<T>;
			static_assert(std::is_base_of_v<Component, C>, "Must inherit from class Component");
			ComponentType id = getComponentType<C>();
			if (mComponentStorageType.contains(id) && mComponentStorageType[id] == StorageType::Sparse) {
				std::static_pointer_cast<ComponentArray<C>>(mSparseArrays[id])->removeData(entity);
			}
		}

		template<typename... Component>
		void addComponentsToArchetype(Entity entity, Archetype& to, Component&&... components) {
			(addComponentToArchetypeImpl<Component>(entity, to, components), ...);
		}

		template<class... Components>
		void moveEntity(Entity entity, std::shared_ptr<Archetype> from, std::shared_ptr<Archetype> to, Record& record, Components&&... components) {
			for (auto& [ctype, cvec] : from->mComponentArrays) {
				if (to->mComponentArrays.contains(ctype)) {
					//moveComponent(entity, to->mComponentArrays.at(ctype), cvec, ctype);
				}
			}

			addComponentsToArchetype(entity, *to, components...);
			record.archetype = to;
		}

		template<typename T>
		RawType<T>& forEachComponentsImpl(Archetype& archetype, size_t i, [[maybe_unused]] Entity entity) {
			using U = RawType<T>;
			static_assert(std::is_base_of_v<Component, U>, "Must inherit from class Component");
			ComponentType id = getComponentType<U>();
			//if (mComponentStorageType[id] == StorageType::Archetype) {
			return archetype.getComponents<U>(id)->at(i);
			//}
			//else {
			//	return std::static_pointer_cast<ComponentArray<U>>(mSparseArrays[id])->getData(entity);
			//}
		}

		template<class... Components, class F>
		void forEachComponents(Archetype& archetype, size_t i, Entity entity, F&& func) {
			func(entity, forEachComponentsImpl<Components>(archetype, i, entity)...);
		}

		template<typename T>
		void emitOnAdd(Entity entity) {
			auto c = getComponent<T>(entity);
			if (c) mEventManager.emit(OnAdd<T>{entity, c.get()});
		}

		template<typename T>
		void emitOnRemove(Entity entity) {
			auto c = getComponent<T>(entity);
			if (c) mEventManager.emit(OnRemove<T>{entity, c.get()});
		}

		template<typename T>
		T& getRef(Entity entity) {
			ComponentType id = getComponentType<T>();
			//if (mComponentStorageType[id] == StorageType::Archetype) {
			//	const Record& record = mEntityRecords[entity];
			//	const auto& archetype = record.archetype;
			//	return std::static_pointer_cast<ComponentArray<T>>(archetype->mComponentArrays.at(id))->getData(entity);
			//}
			//else {
				return std::static_pointer_cast<ComponentArray<T>>(mSparseArrays[id])->getData(entity);
			//}
		}

	public:
		template<class First, class Func>
		void forEachSparse(Func&& func) {
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

		template<typename T>
		ComponentType getComponentTypeImpl() {
			static ComponentType componentId = registerNewComponentId<T>();
			std::cout << typeid(T).name() << " - " << (int)componentId << "\n";
			return componentId;
		}

		template<typename T>
		ComponentType getComponentType() {
			return getComponentTypeImpl<RawType<T>>();
		}

		template<typename T>
		void registerComponent(StorageType type) {
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

		RelationManager& getRelationManager() { return mRelationManager; }
		const RelationManager& getRelationManager() const { return mRelationManager; }
		EventManager& getEventManager() { return mEventManager; }
		const EventManager& getEventManager() const { return mEventManager; }

		template<typename T>
		UTILS::WeakPtr<T> getSingletonComponent() {
			static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
			const auto type = getComponentType<T>();
			if (!mSingletonComponent.contains(type)) {
				return nullptr;
			}
			return mSingletonComponent.at(type)->template getWeak<T>();
		}

		template<typename T>
		UTILS::WeakPtr<T> addSingletonComponent() {
			static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
			const auto type = getComponentType<T>();
			mSingletonComponent[type] = std::make_unique<T>();
			return getSingletonComponent<T>();
		}

		template<typename T>
		UTILS::WeakPtr<T> addComponent(Entity entity) {
			static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
			addComponents<T>(entity, T());
			return getComponent<T>(entity);
		}

		template<typename T>
		UTILS::WeakPtr<T> addComponent(Entity entity, T&& newComponent) {
			static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
			addComponents<T>(entity, std::move(newComponent));
			return getComponent<T>(entity);
		}

		template<class... Components>
		void addComponents(Entity entity) {
			addComponents(entity, (Components(), ...));
		}

		template<class... Components>
		void addComponents(Entity entity, Components&&... newComponent) {
			Record& record = mEntityRecords.at(entity);
			const auto& archetype = record.archetype;
			auto newMask = archetype->mMask;
			// set all components to mask (Only Archetype ones)
			(addComponentToArchetypeMaskImpl<Components>(newMask), ...);
			const auto& nextArchetype = getArchetype(newMask);

			// Move entity (Handles Archetype components)
			moveEntity(entity, archetype, nextArchetype, record, std::forward<Components>(newComponent)...);
			
			// Handle Sparse components
			(addComponentToSparseImpl<Components>(entity, std::forward<Components>(newComponent)), ...);
			(emitOnAdd<Components>(entity), ...);

			//auto signature = getSignature(entity);
			//signature.set(static_cast<int>(getComponentType<T>()), true);
			//setSignature(entity, newMask);
			//systemManager->entitySignatureChanged(entity, newMask);
		}

		template<class... Components>
		void removeComponents(Entity entity) {
			//static_assert(sizeof...(Components) == 0, "Must contain non 0 components");
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

		template<typename T>
		UTILS::WeakPtr<T> getComponent(Entity entity) {
			static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
			ComponentType id = getComponentType<T>();
			
			// Check Archetype first
			const Record& record = mEntityRecords[entity];
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

		template<typename T>
		UTILS::WeakPtr<Component> getComponentBase(Entity entity) {
			return getComponentBase<T>(entity);
		}

		template<class T = Component>
		std::vector<UTILS::WeakPtr<T>> getComponents(Entity entity) {
			std::vector<UTILS::WeakPtr<T>> res;

			if (mEntityRecords.contains(entity)) {
				const Record& record = mEntityRecords.at(entity);
				const auto& archetype = record.archetype;

				for (auto& [id, componentArr] : archetype->mComponentArrays) {
					if (componentArr->contains(entity)) {
						auto basePtr = componentArr->getComponentBasePtr(entity);
						if (basePtr) {
							if constexpr (std::is_same_v<T, Component> || std::is_same_v<T, IKIGAI::ECS2::Component>) {
								res.push_back(std::static_pointer_cast<T>(basePtr));
							} else {
								auto casted = std::dynamic_pointer_cast<T>(basePtr);
								if (casted) res.push_back(casted);
							}
						}
					}
				}
			}

			for (auto& [id, sparseArr] : mSparseArrays) {
				if (sparseArr->contains(entity)) {
					auto basePtr = sparseArr->getComponentBasePtr(entity);
					if (basePtr) {
						if constexpr (std::is_same_v<T, Component> || std::is_same_v<T, IKIGAI::ECS2::Component>) {
							res.push_back(std::static_pointer_cast<T>(basePtr));
						} else {
							auto casted = std::dynamic_pointer_cast<T>(basePtr);
							if (casted) res.push_back(casted);
						}
					}
				}
			}

			return res;
		}

		template<typename T>
		bool checkComponent(Entity entity) {
			static_assert(std::is_base_of_v<Component, T>, "Must inherit from class Component");
			ComponentType id = getComponentType<T>();

			// Archetype check
			const Record& record = mEntityRecords[entity];
			const auto& archetype = record.archetype;
			if (archetype->mComponentArrays.contains(id)) return true;

			// Sparse check
			if (mSparseArrays.contains(id)) {
				// We need to check if entity exists in this array.
				// ComponentArray->getDataPtr returns nullptr if not found?
				// need to check `entityDestroyed` logic or `hasData`.
				// ComponentArray usually has `getDataPtr`.
				return std::static_pointer_cast<ComponentArray<T>>(mSparseArrays[id])->checkComponent(entity);
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
			mEntityRecords[entity] = Record{{}, getArchetype(Signature())};
		}

		void destroyEntity(Entity entity) {
			mEntityRecords.erase(entity);
		}

		template<class... Components, class Func>
		void forEach(Func&& func) {
			Signature mask;
			(addComponentToArchetypeMaskImpl<Components>(mask), ...);

			for (auto& [m, a] : mMaskToArchetype) {
				if ((mask & m) != mask) {
					continue;
				}
				for (size_t i = 0; i < a->size(); ++i) {
					auto entity = a->mComponentArrays.begin()->second->getEntity(i);
					if ((checkComponent<Components>(entity) && ...)) {
						forEachComponents<Components...>(*a, i, entity, func);
					}
				}
			}
		}

		template<class T>
		ComponentArray<T>& getComponentsArray()
		{
			auto type = getComponentType<T>();
			if (mSparseArrays.contains(type)) {
				return *(std::static_pointer_cast<ComponentArray<T>>(mSparseArrays.at(type)));
			}
			ASSERT("Array is not exist");
			//LOG_ERROR("Array does not exist");
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
