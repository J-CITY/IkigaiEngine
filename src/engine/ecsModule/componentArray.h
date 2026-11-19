#pragma once

#include "componentArrayInterface.h"
#include "sparseRowIndex.h"
#include "utilsModule/chunkList.h"
#include "utilsModule/exeptions.h"
#include <numeric>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace IKIGAI::ECS {
template <class T, template <class, class> class CONT = std::vector, unsigned CHUNK_SIZE = 1024>
class ComponentArray : public ComponentArrayInterface {
	using Storage = UTILS::ChunkList<T, CONT, CHUNK_SIZE>;

public:
	static constexpr std::size_t chunk_size = CHUNK_SIZE;
	ComponentArray() = default;
	explicit ComponentArray(const typename Storage::allocator_type& allocator)
		: componentArray(allocator) { }
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
		const auto index = independentSlot(entity).index;
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
		if (mRows || mSparseRows) {
			const auto row = rowOf(entity);
			return row == ArchetypeRows::missing ? 0 : mGenerations[row];
		}
		const auto entry = entityToIndexInArray.find(entity);
		return entry == entityToIndexInArray.end() ? 0 : entry->second.generation;
	}

	const T* tryGetData(Entity entity, std::uint64_t generation) const {
		if (mRows || mSparseRows) {
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
		const auto row = rowOf(entity);
		if (row != ArchetypeRows::missing) {
			eraseRow(entity, row);
		}
	}

	void transferEntityTo(Entity entity, ComponentArrayInterface& destination) override {
		requireIndependentRows();
		const Slot slot = independentSlot(entity);
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
		auto result = std::make_shared<ComponentArray>(componentArray.get_allocator());
		if (mSparseRows) {
			result->mSparseRows.emplace();
		}
		return result;
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
	friend struct EcsBenchmarkAccess;

	struct Slot {
		std::size_t index = 0;
		std::uint64_t generation = 0;
	};

	std::size_t rowOf(Entity entity) const {
		if (mSparseRows) {
			return mSparseRows->find(entity.getUniqueId().value());
		}
		if (mRows) {
			return mRows->find(entity);
		}
		const auto entry = entityToIndexInArray.find(entity);
		return entry == entityToIndexInArray.end() ? ArchetypeRows::missing : entry->second.index;
	}

	Slot independentSlot(Entity entity) const {
		if (!mSparseRows) {
			return entityToIndexInArray.at(entity);
		}
		const auto row = mSparseRows->find(entity.getUniqueId().value());
		if (row == SparseRowIndex::missing) {
			throw std::out_of_range("Entity has no component in this column");
		}
		return { row, mGenerations[row] };
	}

	void setIndependentRow(Entity entity, std::size_t row) {
		if (mSparseRows) {
			mSparseRows->setRow(entity.getUniqueId().value(), row);
		}
		else {
			entityToIndexInArray.at(entity).index = row;
		}
	}

	void requireIndependentRows() const {
		if (mRows) {
			throw std::logic_error("Archetype columns are structurally edited by ComponentManager");
		}
	}

	static auto& generationSequence() {
		static IdSequence<ComponentArray, std::uint64_t, 1> generations;
		return generations;
	}

	static std::uint64_t nextGeneration() {
		return generationSequence().next().value();
	}

	void attachRows(const std::shared_ptr<const ArchetypeRows>& rows) override {
		mRows = rows;
	}

	bool hasTrivialLifecycle() const noexcept override {
		return std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>;
	}

	std::size_t reserveIndexedRows(std::size_t required) override {
		const auto count = componentArray.size();
		if (required > componentArray.capacity()) {
			componentArray.reserve(required);
		}
		if (required > mGenerations.capacity()) {
			// Keep small archetypes compact and copy large generation buffers less often.
			const auto extra = count < 4096 ? count / 2 + 1 : count;
			mGenerations.reserve(std::max(required, count + extra));
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

	void appendIndexedValues(std::size_t count, const T& value)
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_copy_constructible_v<T>)
	{
		appendIndexedValues(count, value, generationSequence().nextRange(count).value());
	}

	void appendIndexedValues(std::size_t count, const T& value, std::uint64_t generation)
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_copy_constructible_v<T>)
	{
		const auto first = mGenerations.size();
		componentArray.append_trivial_fill_reserved(count, value);
		mGenerations.resize(first + count);
		std::iota(mGenerations.begin() + first, mGenerations.end(), generation);
	}

	void transferIndexedRange(std::size_t first, std::size_t count, ComponentArrayInterface& destination) noexcept override {
		if constexpr (std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>) {
			auto& target = static_cast<ComponentArray&>(destination);
			target.componentArray.append_trivial_range_reserved(componentArray, first, count);
			target.mGenerations.insert(target.mGenerations.end(), mGenerations.begin() + first, mGenerations.begin() + first + count);

			const auto tailCount = std::min(count, mGenerations.size() - first - count);
			std::copy_n(mGenerations.end() - tailCount, tailCount, mGenerations.begin() + first);
			mGenerations.resize(mGenerations.size() - count);
			componentArray.erase_trivial_range_unordered(first, count);
		}
		else {
			std::terminate();
		}
	}

	void appendIndexedRows(std::span<const IndexedRowMove> outgoing, ComponentArrayInterface& destination, bool destinationOrder) noexcept override {
		if constexpr (std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>) {
			auto& target = static_cast<ComponentArray&>(destination);
			const auto first = target.mGenerations.size();
			target.mGenerations.resize(first + outgoing.size());
			if (destinationOrder) {
				target.componentArray.append_trivial_gather_reserved(componentArray, outgoing);
				auto generations = target.mGenerations.begin() + first;
				for (const auto& row : outgoing) {
					*generations++ = mGenerations[row.source];
				}
			}
			else {
				target.componentArray.append_trivial_rows_reserved(componentArray, outgoing);
				for (const auto& row : outgoing) {
					target.mGenerations[first + row.destination] = mGenerations[row.source];
				}
			}
		}
		else {
			std::terminate();
		}
	}

	void compactIndexedRows(std::size_t retained, std::span<const IndexedRowMove> compacted) noexcept override {
		if constexpr (std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>) {
			for (const auto& row : compacted) {
				mGenerations[row.destination] = mGenerations[row.source];
			}
			mGenerations.resize(retained);
			componentArray.compact_trivial_rows(retained, compacted);
		}
		else {
			std::terminate();
		}
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
		const auto row = componentArray.size();
		auto legacyEntry = entityToIndexInArray.end();
		if (mSparseRows) {
			mSparseRows->insert(entity.getUniqueId().value(), row);
		}
		else {
			const auto [entry, inserted] = entityToIndexInArray.try_emplace(entity, Slot { row, generation });
			if (!inserted) {
				throw std::logic_error("Component added to the same entity twice");
			}
			legacyEntry = entry;
		}
		try {
			indexToEntity.push_back(entity);
			if (mSparseRows) {
				mGenerations.push_back(generation);
			}
			return componentArray.emplace_back(std::move(component));
		} catch (...) {
			if (indexToEntity.size() > row) {
				indexToEntity.pop_back();
			}
			if (mSparseRows) {
				if (mGenerations.size() > row) {
					mGenerations.pop_back();
				}
				mSparseRows->erase(entity.getUniqueId().value());
			}
			else {
				entityToIndexInArray.erase(legacyEntry);
			}
			throw;
		}
	}

	void popBack(Entity entity) {
		if constexpr (std::is_base_of_v<Component, T>) {
			componentArray.back().invalidateWeakReferences();
		}
		if (mSparseRows) {
			mSparseRows->erase(entity.getUniqueId().value());
			mGenerations.pop_back();
		}
		else {
			entityToIndexInArray.erase(entity);
		}
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
			setIndependentRow(lastEntity, index);
			if (mSparseRows) {
				mGenerations[index] = mGenerations.back();
			}
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
		setIndependentRow(indexToEntity[index], index);
		setIndependentRow(indexToEntity[lastIndex], lastIndex);
		if (mSparseRows) {
			std::swap(mGenerations[index], mGenerations[lastIndex]);
		}
	}

	Storage componentArray;
	std::unordered_map<Entity, Slot> entityToIndexInArray;
	std::optional<SparseRowIndex> mSparseRows;
	std::vector<Entity> indexToEntity;
	std::shared_ptr<const ArchetypeRows> mRows;
	std::vector<std::uint64_t> mGenerations;
};
}
