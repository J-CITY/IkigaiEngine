#pragma once

#include "componentArrayInterface.h"
#include "entityManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/chunkList.h"
#include "utilsModule/exeptions.h"
#include "utilsModule/weakPtr.h"

namespace IKIGAI::ECS {
template <class T, template <class, class> class CONT = std::vector, unsigned CHUNK_SIZE = 100>
class ComponentArray : public ComponentArrayInterface {
public:
	ComponentArray() = default;

	void insert(Entity entity, T&& component) {
		ASSERT_IF(!entityToIndexInArray.contains(entity), "Component added to same entity more than once");

		auto newIndex = size;
		entityToIndexInArray[entity] = newIndex;
		entityToIndexInArray[entity] = newIndex;
		indexToEntity.push_back(entity);
		if (componentArray.size() > newIndex) {
			componentArray[newIndex] = std::forward<T>(component);
		}
		else {
			componentArray.push_back(std::forward<T>(component));
		}
		// Update cb ptr
		componentArray[newIndex].getControlBlock()->mPtr = &componentArray[newIndex];
		size++;
	}

	T removeData(Entity entity) {
		ASSERT_IF(entityToIndexInArray.contains(entity), "Removing non-existent component");

		// Copy element at end into deleted element's place to maintain density
		const size_t indexOfRemovedEntity = entityToIndexInArray[entity];
		const size_t indexOfLastElement = size - 1;
		auto component = std::move(componentArray[indexOfRemovedEntity]);
		component.getControlBlock()->mPtr = nullptr;
		if (indexOfRemovedEntity != indexOfLastElement) {
			componentArray[indexOfRemovedEntity] = std::move(componentArray[indexOfLastElement]);
			componentArray[indexOfRemovedEntity].getControlBlock()->mPtr = &componentArray[indexOfRemovedEntity];
			const Entity entityOfLastElement = indexToEntity.back();
			entityToIndexInArray[entityOfLastElement] = indexOfRemovedEntity;
			indexToEntity[indexOfRemovedEntity] = entityOfLastElement;
		}
		entityToIndexInArray.erase(entity);
		indexToEntity.pop_back();
		size--;

		componentArray.pop_back();
		return component;
	}

	T& getData(Entity entity) {
		ASSERT_IF(entityToIndexInArray.contains(entity), "Retrieving non-existent component");
		return componentArray[entityToIndexInArray[entity]];
	}

	bool checkComponent(Entity entity) {
		// static_assert(std::is_base_of_v<ECS::Component, T>, "Must inherit from class Component");
		return entityToIndexInArray.contains(entity);
	}

	UTILS::WeakPtr<T> getDataPtr(Entity entity) {
		// static_assert(std::is_base_of_v<ECS::Component, T>, "Must inherit from class Component");
		ASSERT_IF(entityToIndexInArray.contains(entity), "Retrieving non-existent component");
		return componentArray[entityToIndexInArray[entity]].template getWeak<T>();
	}

	UTILS::WeakPtr<ECS::Component> getComponentBasePtr(Entity entity) override {
		// static_assert(std::is_base_of_v<ECS::Component, T>, "Must inherit from class Component");
		ASSERT_IF(entityToIndexInArray.contains(entity), "Retrieving non-existent component");
		return UTILS::WeakPtr<ECS::Component>(componentArray[entityToIndexInArray[entity]]);
	}

	//??
	void entityDestroyed(Entity entity) /*override*/ {
		if (entityToIndexInArray.contains(entity)) {
			removeData(entity);
		}
	}

	void transferEntityTo(Entity entity, ComponentArrayInterface& destination) override {
		T data = removeData(entity);
		static_cast<ComponentArray<T>&>(destination).insert(entity, std::move(data));
	}

	std::shared_ptr<ComponentArrayInterface> createEmptyFromThis() override {
		return std::make_shared<ComponentArray<T>>();
	}

	[[nodiscard]] bool contains(Entity entity) const override {
		return entityToIndexInArray.contains(entity);
	}

	[[nodiscard]] int getSize() const /*override*/ {
		return size;
	}

	T& at(size_t i) {
		if (i >= getSize()) {
			throw UTILS::EXEPTIONS::IndexOutOfRange();
		}
		return componentArray[i];
	}

	bool empty() const {
		return size == 0;
	}

	auto begin() {
		return componentArray.begin();
	}
	auto begin() const {
		return componentArray.begin();
	}
	auto end() {
		return componentArray.end();
	}
	auto end() const {
		return componentArray.end();
	}

	Entity getEntity(size_t index) override {
		return indexToEntity[index];
	}

private:
	UTILS::ChunkList<T, CONT, CHUNK_SIZE> componentArray;
	std::unordered_map<Entity, size_t> entityToIndexInArray;
	std::vector<Entity> indexToEntity;
	size_t size = 0;
};
}
