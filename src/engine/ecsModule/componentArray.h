#pragma once

#include "componentArrayInterface.h"
#include "utilsModule/chunkList.h"
#include "utilsModule/exeptions.h"
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace IKIGAI::ECS {
template <class T, template <class, class> class CONT = std::vector, unsigned CHUNK_SIZE = 100>
class ComponentArray : public ComponentArrayInterface {
public:
	ComponentArray() = default;
	ComponentArray(const ComponentArray&) = default;
	ComponentArray(ComponentArray&&) = default;
	ComponentArray& operator=(const ComponentArray&) = default;
	ComponentArray& operator=(ComponentArray&&) = default;

	~ComponentArray() override {
		if constexpr (std::is_base_of_v<Component, T>) {
			for (auto& component : componentArray) {
				component.invalidateWeakReferences();
			}
		}
	}

	void insert(Entity entity, T&& component) {
		requireIndependentRows();
		auto& inserted = append(entity, component, nextGeneration());
		relocateWeakReferences(inserted, component);
	}

	T removeData(Entity entity) {
		requireIndependentRows();
		const auto index = entityToIndexInArray.at(entity).index;
		T component(std::move(componentArray[index]));
		eraseRow(entity, index);
		return component;
	}

	T& getData(Entity entity) {
		const auto row = rowOf(entity);
		if (row == ArchetypeRows::missing) {
			throw std::out_of_range("Entity has no component in this column");
		}
		return componentArray[row];
	}

	T* tryGetData(Entity entity) {
		const auto row = rowOf(entity);
		return row == ArchetypeRows::missing ? nullptr : &componentArray[row];
	}

	const T* tryGetData(Entity entity) const {
		const auto row = rowOf(entity);
		return row == ArchetypeRows::missing ? nullptr : &componentArray[row];
	}

	std::uint64_t getGeneration(Entity entity) const {
		if (mRows) {
			const auto row = rowOf(entity);
			return row == ArchetypeRows::missing ? 0 : mGenerations[row];
		}
		const auto entry = entityToIndexInArray.find(entity);
		return entry == entityToIndexInArray.end() ? 0 : entry->second.generation;
	}

	const T* tryGetData(Entity entity, std::uint64_t generation) const {
		if (mRows) {
			const auto row = rowOf(entity);
			return row != ArchetypeRows::missing && mGenerations[row] == generation ? &componentArray[row] : nullptr;
		}
		const auto entry = entityToIndexInArray.find(entity);
		if (entry == entityToIndexInArray.end() || entry->second.generation != generation) {
			return nullptr;
		}
		return &componentArray[entry->second.index];
	}

	const Component* tryGetBaseData(Entity entity, std::uint64_t generation) const override {
		if constexpr (std::is_base_of_v<Component, T>) {
			return tryGetData(entity, generation);
		}
		return nullptr;
	}

	bool checkComponent(Entity entity) {
		return contains(entity);
	}

	auto getDataPtr(Entity entity)
		requires std::is_base_of_v<Component, T>
	{
		return UTILS::WeakPtr<T>(tryGetData(entity));
	}

	UTILS::WeakPtr<ECS::Component> getComponentBasePtr(Entity entity) override {
		if constexpr (std::is_base_of_v<Component, T>) {
			return UTILS::WeakPtr<Component>(tryGetData(entity));
		}
		return nullptr;
	}

	void entityDestroyed(Entity entity) override {
		requireIndependentRows();
		const auto entry = entityToIndexInArray.find(entity);
		if (entry != entityToIndexInArray.end()) {
			eraseRow(entity, entry->second.index);
		}
	}

	void transferEntityTo(Entity entity, ComponentArrayInterface& destination) override {
		requireIndependentRows();
		const Slot slot = entityToIndexInArray.at(entity);
		const auto index = slot.index;
		auto* other = dynamic_cast<ComponentArray*>(&destination);
		if (!other) {
			throw std::invalid_argument("Cannot transfer between different component array types");
		}
		other->requireIndependentRows();
		if (other == this) {
			moveRowToBack(index);
			return;
		}

		auto& source = componentArray[index];
		auto& inserted = other->append(entity, source, slot.generation);
		relocateWeakReferences(inserted, source);
		try {
			eraseRow(entity, index);
		} catch (...) {
			relocateWeakReferences(inserted, source);
			other->popBack(entity);
			throw;
		}
	}

	std::shared_ptr<ComponentArrayInterface> createEmptyFromThis() override {
		return std::make_shared<ComponentArray>();
	}

	[[nodiscard]] bool contains(Entity entity) const override {
		return rowOf(entity) != ArchetypeRows::missing;
	}

	[[nodiscard]] int getSize() const override {
		return static_cast<int>(componentArray.size());
	}

	T& at(size_t i) {
		if (i >= componentArray.size()) {
			throw UTILS::EXEPTIONS::IndexOutOfRange();
		}
		return componentArray[i];
	}

	bool empty() const {
		return componentArray.empty();
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
		return (mRows ? mRows->entities : indexToEntity).at(index);
	}

	[[nodiscard]] std::span<const Entity> getEntities() const noexcept override {
		return mRows ? mRows->entities : indexToEntity;
	}

private:
	friend class ComponentManager;

	struct Slot {
		std::size_t index = 0;
		std::uint64_t generation = 0;
	};

	std::size_t rowOf(Entity entity) const {
		if (mRows) {
			return mRows->find(entity);
		}
		const auto entry = entityToIndexInArray.find(entity);
		return entry == entityToIndexInArray.end() ? ArchetypeRows::missing : entry->second.index;
	}

	void requireIndependentRows() const {
		if (mRows) {
			throw std::logic_error("Archetype columns are structurally edited by ComponentManager");
		}
	}

	static std::uint64_t nextGeneration() {
		static IdSequence<ComponentArray, std::uint64_t, 1> generations;
		return generations.next().value();
	}

	void attachRows(const std::shared_ptr<const ArchetypeRows>& rows) override {
		mRows = rows;
	}

	std::size_t reserveIndexedRow() override {
		const auto count = componentArray.size();
		if (count == componentArray.capacity()) {
			componentArray.reserve(count + 1);
		}
		if (count == mGenerations.capacity()) {
			mGenerations.reserve(count + count / 2 + 1);
		}
		return std::min(componentArray.capacity(), mGenerations.capacity());
	}

	void appendIndexed(T& component, std::uint64_t generation) noexcept {
		auto& inserted = componentArray.emplace_back(std::move(component));
		mGenerations.push_back(generation);
		relocateWeakReferences(inserted, component);
	}

	void replaceIndexed(std::size_t row, T& component, std::uint64_t generation) noexcept {
		auto& old = componentArray[row];
		if constexpr (std::is_base_of_v<Component, T>) {
			old.invalidateWeakReferences();
		}
		old = std::move(component);
		relocateWeakReferences(old, component);
		mGenerations[row] = generation;
	}

	void appendIndexedMove(std::size_t sourceRow, ComponentArrayInterface& destination) noexcept override {
		static_cast<ComponentArray&>(destination).appendIndexed(componentArray[sourceRow], mGenerations[sourceRow]);
	}

	void eraseIndexedRow(std::size_t row) noexcept override {
		const auto last = componentArray.size() - 1;
		if (row != last) {
			auto& removed = componentArray[row];
			if constexpr (std::is_base_of_v<Component, T>) {
				removed.invalidateWeakReferences();
			}
			auto& moved = componentArray[last];
			removed = std::move(moved);
			relocateWeakReferences(removed, moved);
			mGenerations[row] = mGenerations[last];
		}
		if constexpr (std::is_base_of_v<Component, T>) {
			componentArray.back().invalidateWeakReferences();
		}
		componentArray.pop_back();
		mGenerations.pop_back();
	}

	static void relocateWeakReferences(T& destination, T& source) noexcept {
		if constexpr (std::is_base_of_v<Component, T>) {
			destination.swapControlBlocks(source);
		}
	}

	T& append(Entity entity, T& component, std::uint64_t generation) {
		const auto [entry, inserted] = entityToIndexInArray.try_emplace(entity, Slot { componentArray.size(), generation });
		if (!inserted) {
			throw std::logic_error("Component added to the same entity twice");
		}
		try {
			indexToEntity.push_back(entity);
			try {
				return componentArray.emplace_back(std::move(component));
			} catch (...) {
				indexToEntity.pop_back();
				throw;
			}
		} catch (...) {
			entityToIndexInArray.erase(entry);
			throw;
		}
	}

	void popBack(Entity entity) {
		if constexpr (std::is_base_of_v<Component, T>) {
			componentArray.back().invalidateWeakReferences();
		}
		entityToIndexInArray.erase(entity);
		indexToEntity.pop_back();
		componentArray.pop_back();
	}

	void eraseRow(Entity entity, size_t index) {
		if (index != componentArray.size() - 1) {
			auto& removed = componentArray[index];
			auto& last = componentArray.back();
			removed = std::move(last);
			relocateWeakReferences(removed, last);
			const Entity lastEntity = indexToEntity.back();
			entityToIndexInArray.at(lastEntity).index = index;
			indexToEntity[index] = lastEntity;
		}
		popBack(entity);
	}

	void moveRowToBack(size_t index) {
		const auto lastIndex = componentArray.size() - 1;
		if (index == lastIndex) {
			return;
		}
		auto& selected = componentArray[index];
		auto& last = componentArray.back();
		std::swap(selected, last);
		relocateWeakReferences(selected, last);
		std::swap(indexToEntity[index], indexToEntity[lastIndex]);
		entityToIndexInArray.at(indexToEntity[index]).index = index;
		entityToIndexInArray.at(indexToEntity[lastIndex]).index = lastIndex;
	}

	UTILS::ChunkList<T, CONT, CHUNK_SIZE> componentArray;
	std::unordered_map<Entity, Slot> entityToIndexInArray;
	std::vector<Entity> indexToEntity;
	std::shared_ptr<const ArchetypeRows> mRows;
	std::vector<std::uint64_t> mGenerations;
};
}
