#pragma once
#include <map>
#include <memory>
#include <unordered_map>

#include "componentArray.h"
#include "componentArrayInterface.h"
#include "componentTypes.h"
#include "systemManager.h"

//#include "components/physicsComponent.h"
//#include "components/AmbientLight.h"
//#include "components/AmbientSphereLight.h"
//#include "components/audioComponent.h"
//#include "components/cameraComponent.h"
//#include "components/directionalLight.h"
//#include "components/inputComponent.h"
//#include "components/logicComponent.h"
//#include "components/modelRenderer.h"
//#include "components/materialRenderer.h"
//#include "components/pointLight.h"
//#include "components/scriptComponent.h"
//#include "components/skeletal.h"
//#include "components/spotLight.h"

namespace IKIGAI {
	namespace SCENE_SYSTEM {
		class Scene;
	}
}

namespace IKIGAI::ECS {
	//struct ArchetypeComponentArray {
	//public:
	//	std::vector<void> mElements;
	//	size_t mElementSize = 0;
	//	size_t mSize = 0;
	//};
	class Archetype {
	public:
		Signature mask;

		using ComponentType = std::string;
		std::unordered_map<ComponentType, std::shared_ptr<ComponentArrayInterface>> componentArrays;
		//std::unordered_map<Entity, size_t> entityToId;
		//std::unordered_map<size_t, Entity> idToEntity;


		//std::unordered_map<ComponentId, ArchetypeEdge> edges;

		template<class T>
		std::shared_ptr<ComponentArray<T>> getComponents() {
			auto cname = typeid(T).name();
			if (!componentArrays.contains(cname)) {
				return nullptr;
			}
			return std::static_pointer_cast<ComponentArray<T>>(componentArrays[cname]);
		}
	};

	struct Record {
		std::shared_ptr<Archetype> archetype;
		//std::optional<size_t> row;
	};

	template<typename T>
	void moveComponentImpl(Entity entity, std::shared_ptr<ComponentArrayInterface> arrTo, std::shared_ptr<ComponentArrayInterface> arrFrom, const std::string& tname) {
		auto t = IKIGAI::ECS::GetType<T>();
		if (tname == t) {
			std::static_pointer_cast<ComponentArray<T>>(arrTo)->insertData(entity, std::static_pointer_cast<ComponentArray<T>>(arrFrom)->removeData(entity));
		}
	}

	template<template<typename...> class Container, typename...ComponentType>
	void moveComponent(Entity entity, std::shared_ptr<ComponentArrayInterface> arrTo, std::shared_ptr<ComponentArrayInterface> arrFrom, const std::string& tname, [[maybe_unused]] Container<ComponentType...> opt) {
		(moveComponentImpl<ComponentType>(entity, arrTo, arrFrom, tname), ...);
	}

	inline void moveComponent(Entity entity, std::shared_ptr<ComponentArrayInterface> arrTo, std::shared_ptr<ComponentArrayInterface> arrFrom, const std::string& tname) {
		moveComponent(entity, arrTo, arrFrom, tname, IKIGAI::ECS::ComponentsTypeProviderType{});
	}

	class ComponentManager : public UTILS::SingletonService<ComponentManager> {
		FRIEND_SINGLETON_SERVICE(ComponentManager)
		std::unique_ptr<SystemManager> systemManager;
		ComponentManager();

		std::unordered_map<Entity, Signature> signatures;

	public:
		using ComponentType = Id<ComponentManager>;

	private:
		ComponentType nextComponentType = IdGenerator<ComponentManager>::generateId();

		std::unordered_map<std::string, ComponentType> componentTypes;
		std::unordered_map<std::string, std::shared_ptr<ComponentArrayInterface>> componentArrays;
		std::unordered_map<std::string, std::shared_ptr<ComponentArrayInterface>> componentArraysOff;

		//Archetype
		std::unordered_map<Entity, Record> entityRecords;
		std::unordered_map<Signature, std::shared_ptr<Archetype>> maskToArchetype;
		std::unordered_map<std::string, std::shared_ptr<ComponentArrayInterface>> defaultArraysArchetype;
	public:

		template<typename T>
		void registerComponent() {
			auto typeName = GetType<T>();
			if (typeName.empty()) {
				//TODO: remove it in future
				typeName = typeid(T).name();
			}

			assert(!componentTypes.count(typeName) && "Registering component type more than once.");

			// Add this component type to the component type map
			componentTypes.insert({ typeName, nextComponentType });

			// Create a ComponentArray pointer and add it to the component arrays map
			componentArrays.insert({ typeName, std::make_shared<ComponentArray<T>>() });
			componentArraysOff.insert({ typeName, std::make_shared<ComponentArray<T>>() });
			
			nextComponentType = IdGenerator<ComponentManager>::generateId();
		}

		template<typename T>
		ComponentType getComponentType() {
			auto typeName = GetType<T>();
			//const char* typeName = typeid(T).name();
			//std::cout << typeName << std::endl;
			assert(componentTypes.count(typeName) && "getComponentType Component not registered before use.");
			
			return componentTypes.at(typeName);
		}
		
		void setSignature(Entity entity, Signature signature);

		Signature getSignature(Entity entity);

		// System methods
		template<typename T>
		std::shared_ptr<T> registerSystem() {
			static_assert(std::is_base_of_v<System, T>, "Must inherit from class Component");
			return systemManager->registerSystem<T>();
		}

		template<typename T>
		void setSystemSignature(Signature signature) {
			static_assert(std::is_base_of_v<System, T>, "Must inherit from class Component");
			systemManager->setSignature<T>(signature);
		}

		SystemManager& getSystemManager();

		template<typename T>
		void addComponent(Entity entity, T& component) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			getComponentArray<T>()->insertData(entity, component);

			auto signature = getSignature(entity);
			signature.set(static_cast<int>(getComponentType<T>()), true);
			setSignature(entity, signature);

			systemManager->entitySignatureChanged(entity, signature);
		}

		template<typename T>
		void removeComponent(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			getComponentArray<T>()->removeData(entity);

			auto signature = getSignature(entity);
			signature.set(static_cast<int>(getComponentType<T>()), false);
			setSignature(entity, signature);

			systemManager->entitySignatureChanged(entity, signature);
		}

		template<typename T>
		bool checkComponent(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			return getComponentArray<T>()->count(entity);
		}

		template<typename T>
		UTILS::Ref<T> getComponentRef(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			return getComponentArray<T>()->getData(entity);
		}

		template<typename T>
		UTILS::WeakPtr<T> getComponent(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			return getComponentArray<T>()->getDataPtr(entity);
		}

		template<typename T>
		UTILS::WeakPtr<ComponentBase> getComponentBase(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			return getComponentArray<T>()->getDataBasePtr(entity);
		}


		//Call in scene? when object create
		void createEntity(Entity entity) {
			entityRecords[entity] = Record{getArchetype(Signature())};
		}
		//TODO:: change key to component type
		std::shared_ptr<Archetype> getArchetype(Signature mask) {
			if (!maskToArchetype.contains(mask)) {
				maskToArchetype[mask] = std::make_shared<Archetype>();
				auto newArchetype = maskToArchetype[mask];
				newArchetype->mask = mask;
				for (const auto& [name, id] : componentTypes) {
					if (mask[static_cast<size_t>(id)]) {
						newArchetype->componentArrays[name] = defaultArraysArchetype[name]->createEmptyFromThis();
					}
				}
			}
			return maskToArchetype[mask];
		}

		template<typename T>
		void registerComponentArchetype() {
			auto typeName = typeid(T).name();

			assert(!defaultArraysArchetype.count(typeName) && "Registering component type more than once.");

			// Add this component type to the component type map
			componentTypes.insert({ typeName, nextComponentType });

			// Create a ComponentArray pointer and add it to the component arrays map
			defaultArraysArchetype.insert({ typeName, std::make_shared<ComponentArray<T>>() });

			nextComponentType = IdGenerator<ComponentManager>::generateId();
		}

		template<typename T0>
		void forEachComponents(std::function<void(T0&)> func) {
			for (auto& [m, a] : maskToArchetype) {
				auto& arr0 = a->getComponents<T0>();
				if (arr0) {
					for (size_t i = 0; i < arr0->getSize(); ++i) {
						func(arr0->at(i));
					}
				}
			}
		}

		template<typename T0, typename T1>
		void forEachComponents(std::function<void(T0&, T1&)> func) {
			for (auto& [m, a] : maskToArchetype) {
				auto arr0 = a->getComponents<T0>();
				auto arr1 = a->getComponents<T1>();
				if (arr0 && arr1) {
					for (size_t i = 0; i < arr0->getSize(); ++i) {
						func(arr0->at(i), arr1->at(i));
					}
				}
			}
		}

		template<typename T0, typename T1, typename T2>
		void forEachComponents(std::function<void(T0&, T1&, T2&)> func) {
			for (auto& [m, a] : maskToArchetype) {
				auto arr0 = a->getComponents<T0>();
				auto arr1 = a->getComponents<T1>();
				auto arr2 = a->getComponents<T2>();
				if (arr0 && arr1 && arr2) {
					for (size_t i = 0; i < arr0->getSize(); ++i) {
						func(arr0->at(i), arr1->at(i), arr2->at(i));
					}
				}
			}
		}

		template<typename T>
		void moveEntity(Entity entity, std::shared_ptr<Archetype> from, std::shared_ptr<Archetype> to, Record& record) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			//if (record.archetype) {
				//move all component to new Archetype
				for (auto& [ctype, cvec] : from->componentArrays) {
					if (to->componentArrays.contains(ctype)) {
						moveComponent(entity, to->componentArrays.at(ctype), cvec, ctype);
					}
				}

				//delete components from prev Archetype
				//for (auto& [ctype, cvec] : from->componentArrays) {
				//	cvec->removeDataAny(entity);
				//}
			//}
			const auto cname = typeid(T).name();
			if (to->componentArrays.contains(cname)) {
				T newComponent;
				std::static_pointer_cast<ComponentArray<T>>(to->componentArrays.at(cname))->insertData(entity, newComponent);
			}

			record.archetype = to;
			//if (to->componentArrays.empty()) {
			//	record.archetype = nullptr;
			//}
		}

		template<typename T>
		UTILS::WeakPtr<T> addComponentArchetype(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			Record& record = entityRecords.at(entity);
			const auto archetype = record.archetype;
			auto newMask = archetype->mask;

			const auto cname = typeid(T).name();
			
			newMask.set(static_cast<unsigned>(componentTypes.at(cname)), true);
			const auto nextArchetype = getArchetype(newMask);

			moveEntity<T>(entity, archetype, nextArchetype, record);

			auto signature = getSignature(entity);
			signature.set(static_cast<int>(getComponentType<T>()), true);
			setSignature(entity, signature);
			systemManager->entitySignatureChanged(entity, signature);

			return getComponentArchetype<T>(entity);
		}

		template<typename T>
		UTILS::WeakPtr<T> addComponentArchetype(Entity entity, T& newComponent) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			Record& record = entityRecords.at(entity);
			const auto archetype = record.archetype;
			auto newMask = archetype->mask;

			const auto cname = typeid(T).name();

			newMask.set(static_cast<unsigned>(componentTypes.at(cname)), true);
			const auto nextArchetype = getArchetype(newMask);

			moveEntity<T>(entity, archetype, nextArchetype, record);

			auto signature = getSignature(entity);
			signature.set(static_cast<int>(getComponentType<T>()), true);
			setSignature(entity, signature);
			systemManager->entitySignatureChanged(entity, signature);

			*getComponentArchetype<T>(entity) = newComponent;
			return getComponentArchetype<T>(entity);
		}

		template<typename T>
		void removeComponentArchetype(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			Record& record = entityRecords[entity];
			auto archetype = record.archetype;

			auto cname = typeid(T).name();
			auto newMask = archetype->mask;
			newMask.set(static_cast<unsigned>(componentTypes.at(cname)), false);
			auto nextArchetype = getArchetype(newMask);

			auto signature = getSignature(entity);
			signature.set(static_cast<int>(getComponentType<T>()), false);
			setSignature(entity, signature);
			systemManager->entitySignatureChanged(entity, signature);

			moveEntity<T>(entity, archetype, nextArchetype, record);
		}

		template<typename T>
		UTILS::WeakPtr<T> getComponentArchetype(Entity entity) {
			static_assert(std::is_base_of_v<ComponentBase, T>, "Must inherit from class Component");
			Record& record = entityRecords[entity];
			auto archetype = record.archetype;

			auto cname = typeid(T).name();
			if (!archetype->componentArrays.contains(cname)) {
				return nullptr;
			}

			auto& componentArr = archetype->componentArrays.at(cname);
			return std::static_pointer_cast<ComponentArray<T>>(componentArr)->getDataPtr(entity);
		}
	private:
		template <typename T>
		void tryGetComponent([[maybe_unused]] std::vector<UTILS::WeakPtr<ComponentBase>>& res, [[maybe_unused]] Entity entity) {
			//auto arr = getComponentArray<T>();
			//if (arr->count(entity)) {
			//	res.push_back(arr->getDataBasePtr(entity));
			//}
		}

		template<template<typename...> class Container, typename...ComponentType>
		std::vector<UTILS::WeakPtr<ComponentBase>> tryGetComponents(Entity entity, [[maybe_unused]] Container<ComponentType...> opt) {
			std::vector<UTILS::WeakPtr<ComponentBase>> res;
			(tryGetComponent<ComponentType>(res, entity), ...);
			return res;
		}
	public:
		std::vector<UTILS::WeakPtr<ComponentBase>> getComponents(Entity entity) {
			return tryGetComponents(entity, ComponentsTypeProviderType{});
		}

		void entityDestroyed(Entity entity);

		template<typename T>
		std::shared_ptr<ECS::ComponentArray<T>> getComponentArray() {
			auto typeName = GetType<T>();
			if (typeName.empty()) {
				//TODO: remove it in future
				typeName = typeid(T).name();
			}
			assert(componentTypes.count(typeName) && "getComponentArray Component not registered before use.");
			//std::cout << typeName << std::endl;
			return std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeName]);
		}

		template<typename T>
		ECS::ComponentArray<T>& getComponentArrayRef() {
			auto typeName = GetType<T>();
			if (typeName.empty()) {
				//TODO: remove it in future
				typeName = typeid(T).name();
			}
			assert(componentTypes.count(typeName) && "getComponentArrayRef Component not registered before use.");
			//std::cout << typeName << std::endl;
			return *std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeName]);
		}

		template<typename T>
		std::shared_ptr<ComponentArray<T>> getComponentArrayOff() {
			auto typeName = GetType<T>();
			if (typeName.empty()) {
				//TODO: remove it in future
				typeName = typeid(T).name();
			}
			assert(componentTypes.count(typeName) && "getComponentArrayOff Component not registered before use.");
			//std::cout << typeName << std::endl;
			return std::static_pointer_cast<ComponentArray<T>>(componentArraysOff[typeName]);
		}

	private:
		template<typename T>
		void enabledComponentImpl(Entity id) {
			if (getComponentArrayOff<T>()->count(id)) {
				auto data = getComponentArrayOff<T>()->removeData(id);
				getComponentArray<T>()->insertData(id, std::move(data));
			}
		}

		template<template<typename...> class Container, typename...ComponentType>
		void enabledComponent(Entity id, [[maybe_unused]] Container<ComponentType...> opt) {
			(enabledComponentImpl<ComponentType>(id), ...);
		}

	public:
		void enable(Entity id);
		void disable(Entity id);
	private:
		template<typename T>
		void disableComponentImpl(Entity id) {
			if (getComponentArray<T>()->count(id)) {
				auto data = getComponentArray<T>()->removeData(id);
				getComponentArrayOff<T>()->insertData(id, std::move(data));
			}
		}

		template<template<typename...> class Container, typename...ComponentType>
		void disableComponent(Entity id, [[maybe_unused]] Container<ComponentType...> opt) {
			(disableComponentImpl<ComponentType>(id), ...);
		}
	};
	/*
	struct PosC: public Component {
		PosC(){}
		PosC(int x, int y):Component(), x(x), y(y) {}
		int x = 0;
		int y = 0;
	};

	struct ColorC : public Component {
		ColorC(int r, int g, int b) :Component(), r(r), g(g), b(b) {}
		ColorC(){}
		int r = 0;
		int g = 0;
		int b = 0;
	};

	struct NameC : public Component {
		NameC(){}
		NameC(std::string name) :Component(), name(name) {}
		std::string name;
	};

	struct SystemA: public System {
		void update() {
			ECS::ComponentManager::GetInstance().forEachComponents<PosC, ColorC, NameC>([](PosC& pos, ColorC& color, NameC& name) {
				std::cout << "PosC: " << pos.x << " " << pos.y << std::endl;
				std::cout << "ColorC: " << color.r << " " << color.g << " " << color.b << std::endl;
				std::cout << "NameC: " << name.name << std::endl;
			});
		}
	};

	struct SystemB : public System
	{
		void update() {
			ECS::ComponentManager::GetInstance().forEachComponents<PosC, NameC>([](PosC& pos, NameC& name) {
				std::cout << "PosC: " << pos.x << " " << pos.y << std::endl;
				std::cout << "NameC: " << name.name << std::endl;
			});
		}
	};
	*/

	//void TestECS();
}
