#pragma once
#include "utilsModule/memory/resourceAllocator.h"
#include <algorithm>
#include <cassert>
#include <compare>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace IKIGAI::UTILS {
// Append-only segmented storage. Growth preserves element addresses and existing
// iterators (not end()). Allocators must have raw pointers and nonthrowing frees.
template <class T, std::size_t ChunkSize = 100, class Allocator = MEMORY::StlAllocator<T>> class ChunkedVector {
	static_assert(ChunkSize > 0);
	using Traits = std::allocator_traits<Allocator>;
	using DirectoryAllocator = typename Traits::template rebind_alloc<T*>;
	using Directory = std::vector<T*, DirectoryAllocator>;
	static_assert(std::is_same_v<typename Traits::pointer, T*>);

public:
	using value_type = T;
	using allocator_type = Allocator;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	static constexpr size_type chunk_size = ChunkSize;
	ChunkedVector()
		: ChunkedVector(Allocator()) { }
	explicit ChunkedVector(const Allocator& allocator)
		: mAllocator(allocator)
		, mBlocks(DirectoryAllocator(allocator)) { }
	ChunkedVector(const ChunkedVector& other)
		requires std::is_copy_constructible_v<T>
		: ChunkedVector(other, Traits::select_on_container_copy_construction(other.mAllocator)) { }
	ChunkedVector(const ChunkedVector& other, const Allocator& allocator)
		requires std::is_copy_constructible_v<T>
		: ChunkedVector(allocator) {
		try {
			reserve(other.size());
			for (const auto& value : other) {
				emplace_back(value);
			}
		} catch (...) {
			clear();
			freeBlocks();
			throw;
		}
	}
	ChunkedVector(ChunkedVector&& other)
		: mAllocator(other.mAllocator)
		, mBlocks(std::move(other.mBlocks), DirectoryAllocator(mAllocator))
		, mSize(std::exchange(other.mSize, 0)) {
		other.mBlocks.clear();
	}
	~ChunkedVector() {
		clear();
		freeBlocks();
	}
	ChunkedVector& operator=(const ChunkedVector& other)
		requires std::is_copy_constructible_v<T>
	{
		if (this == &other) {
			return *this;
		}
		if constexpr (Traits::propagate_on_container_copy_assignment::value) {
			if (mAllocator != other.mAllocator) {
				clear();
				freeBlocks();
				const Directory empty { DirectoryAllocator(other.mAllocator) };
				mBlocks = empty;
			}
			mAllocator = other.mAllocator;
		}
		ChunkedVector copy(other, mAllocator);
		swap(copy);
		return *this;
	}
	ChunkedVector& operator=(ChunkedVector&& other) {
		if (this == &other) {
			return *this;
		}
		if constexpr (Traits::propagate_on_container_move_assignment::value) {
			clear();
			freeBlocks();
			mBlocks = std::move(other.mBlocks);
			mAllocator = other.mAllocator;
			mSize = std::exchange(other.mSize, 0);
			other.mBlocks.clear();
		}
		else if (mAllocator == other.mAllocator) {
			clear();
			freeBlocks();
			mBlocks = std::move(other.mBlocks);
			mSize = std::exchange(other.mSize, 0);
			other.mBlocks.clear();
		}
		else {
			if constexpr (std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>) {
				ChunkedVector copy(mAllocator);
				copy.reserve(other.size());
				for (auto& value : other) {
					copy.emplace_back(std::move_if_noexcept(value));
				}
				swap(copy);
				other.clear();
			}
			else {
				throw std::invalid_argument("Cannot move immovable elements between allocation domains");
			}
		}
		return *this;
	}
	void swap(ChunkedVector& other) {
		if constexpr (Traits::propagate_on_container_swap::value) {
			using std::swap;
			swap(mAllocator, other.mAllocator);
		}
		else if (mAllocator != other.mAllocator) {
			throw std::invalid_argument("Incompatible allocation domains");
		}
		mBlocks.swap(other.mBlocks);
		std::swap(mSize, other.mSize);
	}
	friend void swap(ChunkedVector& a, ChunkedVector& b) {
		a.swap(b);
	}
	allocator_type get_allocator() const {
		return mAllocator;
	}
	size_type size() const noexcept {
		return mSize;
	}
	bool empty() const noexcept {
		return mSize == 0;
	}
	bool isEmpty() const noexcept {
		return empty();
	}
	size_type capacity() const noexcept {
		return mBlocks.size() * ChunkSize;
	}
	size_type max_size() const noexcept {
		return std::min(Traits::max_size(mAllocator), static_cast<size_type>(PTRDIFF_MAX)) / ChunkSize * ChunkSize;
	}
	void reserve(size_type count) {
		if (count > max_size()) {
			throw std::length_error("ChunkedVector capacity overflow");
		}
		const auto blocks = count / ChunkSize + (count % ChunkSize != 0);
		if (blocks > mBlocks.capacity()) {
			const auto growth = mBlocks.capacity() + std::min(mBlocks.capacity() / 2 + 1, mBlocks.max_size() - mBlocks.capacity());
			mBlocks.reserve(std::max(blocks, growth));
		}
		while (mBlocks.size() < blocks) {
			auto* block = Traits::allocate(mAllocator, ChunkSize);
			try {
				mBlocks.push_back(block);
			} catch (...) {
				Traits::deallocate(mAllocator, block, ChunkSize);
				throw;
			}
		}
	}
	template <class... Args> T& emplace_back(Args&&... args) {
		if (mSize == max_size()) {
			throw std::length_error("ChunkedVector size overflow");
		}
		if (mSize == capacity()) {
			reserve(mSize + 1);
		}
		T* ptr = mBlocks[mSize / ChunkSize] + mSize % ChunkSize;
		Traits::construct(mAllocator, ptr, std::forward<Args>(args)...);
		++mSize;
		return *ptr;
	}
	void push_back(const T& value) {
		emplace_back(value);
	}
	void push_back(T&& value) {
		emplace_back(std::move(value));
	}
	// The caller reserves storage before changing any live rows across containers.
	void append_trivial_range_reserved(const ChunkedVector& source, size_type first, size_type count) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		assert(this != &source && first <= source.mSize && count <= source.mSize - first && count <= capacity() - mSize);
		copyTrivialRange(mSize, source, first, count);
		mSize += count;
	}
	void append_trivial_fill_reserved(size_type count, const T& value) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_copy_constructible_v<T>)
	{
		assert(count <= capacity() - mSize);
		while (count != 0) {
			const auto offset = mSize % ChunkSize;
			const auto length = std::min(count, ChunkSize - offset);
			std::uninitialized_fill_n(mBlocks[mSize / ChunkSize] + offset, length, value);
			mSize += length;
			count -= length;
		}
	}
	// Fill the removed range from the tail. Retained rows need not keep their order.
	void erase_trivial_range_unordered(size_type first, size_type count) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		assert(first <= mSize && count <= mSize - first);
		const auto tailCount = std::min(count, mSize - first - count);
		copyTrivialRange(first, *this, mSize - tailCount, tailCount);
		mSize -= count;
	}
	// Destination offsets form a permutation of the reserved append range.
	template <class Row>
	void append_trivial_rows_reserved(const ChunkedVector& source, std::span<const Row> rows) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		assert(this != &source && rows.size() <= capacity() - mSize);
		copyTrivialRows(mSize, source, rows);
		mSize += rows.size();
	}
	// The rows are indexed in destination order, so each destination chunk is resolved once.
	template <class Row>
	void append_trivial_gather_reserved(const ChunkedVector& source, std::span<const Row> rows) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		assert(this != &source && rows.size() <= capacity() - mSize);
		while (!rows.empty()) {
			const auto offset = mSize % ChunkSize;
			const auto count = std::min(ChunkSize - offset, rows.size());
			auto* output = mBlocks[mSize / ChunkSize] + offset;
			for (std::size_t index = 0; index < count; ++index) {
				std::memcpy(output + index, std::addressof(source[rows[index].source]), sizeof(T));
			}
			mSize += count;
			rows = rows.subspan(count);
		}
	}
	// Each copy reads a distinct survivor in the discarded tail.
	template <class Row>
	void compact_trivial_rows(size_type retained, std::span<const Row> rows) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		assert(retained <= mSize);
		copyTrivialRows(0, *this, rows);
		mSize = retained;
	}
	void pop_back() noexcept {
		assert(mSize);
		--mSize;
		Traits::destroy(mAllocator, mBlocks[mSize / ChunkSize] + mSize % ChunkSize);
	}
	void clear() noexcept {
		while (mSize) {
			pop_back();
		}
	}
	void release() {
		if (mSize) {
			throw std::logic_error("Release requires an empty ChunkedVector");
		}
		freeBlocks();
		mBlocks.shrink_to_fit();
	}
	T& operator[](size_type index) noexcept {
		return mBlocks[index / ChunkSize][index % ChunkSize];
	}
	const T& operator[](size_type index) const noexcept {
		return mBlocks[index / ChunkSize][index % ChunkSize];
	}
	T& at(size_type index) {
		if (index >= mSize) {
			throw std::out_of_range("ChunkedVector index");
		}
		return (*this)[index];
	}
	const T& at(size_type index) const {
		if (index >= mSize) {
			throw std::out_of_range("ChunkedVector index");
		}
		return (*this)[index];
	}
	T& back() noexcept {
		assert(mSize);
		return (*this)[mSize - 1];
	}
	const T& back() const noexcept {
		assert(mSize);
		return (*this)[mSize - 1];
	}
	size_type chunk_count() const noexcept {
		return mSize / ChunkSize + (mSize % ChunkSize != 0);
	}
	std::span<T> chunk(size_type index) {
		if (index >= chunk_count()) {
			throw std::out_of_range("Chunk index");
		}
		return { mBlocks[index], std::min(ChunkSize, mSize - index * ChunkSize) };
	}
	std::span<const T> chunk(size_type index) const {
		if (index >= chunk_count()) {
			throw std::out_of_range("Chunk index");
		}
		return { mBlocks[index], std::min(ChunkSize, mSize - index * ChunkSize) };
	}

	template <bool Const> class BasicIterator {
		friend class ChunkedVector;
		template <bool> friend class BasicIterator;
		using Owner = std::conditional_t<Const, const ChunkedVector, ChunkedVector>;
		Owner* mOwner = nullptr;
		size_type mIndex = 0;
		BasicIterator(Owner* owner, size_type index)
			: mOwner(owner)
			, mIndex(index) { }

	public:
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using reference = std::conditional_t<Const, const T&, T&>;
		using pointer = std::conditional_t<Const, const T*, T*>;
		using iterator_category = std::random_access_iterator_tag;
		using iterator_concept = std::random_access_iterator_tag;
		BasicIterator() = default;
		template <bool C = Const>
			requires C
		BasicIterator(const BasicIterator<false>& other)
			: mOwner(other.mOwner)
			, mIndex(other.mIndex) { }
		reference operator*() const {
			return (*mOwner)[mIndex];
		}
		pointer operator->() const {
			return std::addressof(**this);
		}
		reference operator[](difference_type offset) const {
			return *(*this + offset);
		}
		BasicIterator& operator++() {
			++mIndex;
			return *this;
		}
		BasicIterator operator++(int) {
			auto old = *this;
			++*this;
			return old;
		}
		BasicIterator& operator--() {
			--mIndex;
			return *this;
		}
		BasicIterator operator--(int) {
			auto old = *this;
			--*this;
			return old;
		}
		BasicIterator& operator+=(difference_type n) {
			mIndex = static_cast<size_type>(static_cast<difference_type>(mIndex) + n);
			return *this;
		}
		BasicIterator& operator-=(difference_type n) {
			return *this += -n;
		}
		friend BasicIterator operator+(BasicIterator it, difference_type n) {
			return it += n;
		}
		friend BasicIterator operator+(difference_type n, BasicIterator it) {
			return it += n;
		}
		friend BasicIterator operator-(BasicIterator it, difference_type n) {
			return it -= n;
		}
		template <bool C> difference_type operator-(const BasicIterator<C>& other) const {
			assert(mOwner == other.mOwner);
			return static_cast<difference_type>(mIndex) - static_cast<difference_type>(other.mIndex);
		}
		template <bool C> bool operator==(const BasicIterator<C>& other) const {
			return mOwner == other.mOwner && mIndex == other.mIndex;
		}
		template <bool C> auto operator<=>(const BasicIterator<C>& other) const {
			assert(mOwner == other.mOwner);
			return mIndex <=> other.mIndex;
		}
	};
	using iterator = BasicIterator<false>;
	using const_iterator = BasicIterator<true>;
	iterator begin() noexcept {
		return { this, 0 };
	}
	iterator end() noexcept {
		return { this, mSize };
	}
	const_iterator begin() const noexcept {
		return { this, 0 };
	}
	const_iterator end() const noexcept {
		return { this, mSize };
	}
	const_iterator cbegin() const noexcept {
		return begin();
	}
	const_iterator cend() const noexcept {
		return end();
	}

private:
	template <class Row>
	void copyTrivialRows(size_type offset, const ChunkedVector& source, std::span<const Row> rows) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		auto sourceChunk = source.mBlocks.size();
		const T* block = nullptr;
		for (const auto& row : rows) {
			const auto chunk = row.source / ChunkSize;
			if (chunk != sourceChunk) {
				sourceChunk = chunk;
				block = source.mBlocks[chunk];
			}
			const auto destination = offset + row.destination;
			std::memcpy(mBlocks[destination / ChunkSize] + destination % ChunkSize, block + row.source % ChunkSize, sizeof(T));
		}
	}

	void copyTrivialRange(size_type destination, const ChunkedVector& source, size_type first, size_type count) noexcept
		requires(std::is_trivially_copyable_v<T> && std::is_trivially_move_constructible_v<T>)
	{
		while (count != 0) {
			const auto destinationOffset = destination % ChunkSize;
			const auto sourceOffset = first % ChunkSize;
			const auto length = std::min({ count, ChunkSize - destinationOffset, ChunkSize - sourceOffset });
			std::memcpy(mBlocks[destination / ChunkSize] + destinationOffset, source.mBlocks[first / ChunkSize] + sourceOffset, length * sizeof(T));
			destination += length;
			first += length;
			count -= length;
		}
	}

	void freeBlocks() noexcept {
		for (auto* block : mBlocks) {
			Traits::deallocate(mAllocator, block, ChunkSize);
		}
		mBlocks.clear();
	}
	Allocator mAllocator;
	Directory mBlocks;
	size_type mSize = 0;
};
}
