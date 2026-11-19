#include "sparseRowIndex.h"

#include <cassert>
#include <limits>
#include <stdexcept>

namespace IKIGAI::ECS {
SparseRowIndex::SparseRowIndex(const SparseRowIndex& other)
	: mDirectories(other.mDirectories.size()) {
	for (std::size_t index = 0; index < mDirectories.size(); ++index) {
		const auto& source = other.mDirectories[index];
		if (!source) {
			continue;
		}
		auto& target = mDirectories[index] = std::make_unique<Directory>();
		target->live = source->live;
		for (std::size_t page = 0; page < directorySize; ++page) {
			if (source->pages[page]) {
				target->pages[page] = std::make_unique<Page>(*source->pages[page]);
			}
		}
	}
}

SparseRowIndex& SparseRowIndex::operator=(const SparseRowIndex& other) {
	if (this != &other) {
		*this = SparseRowIndex(other);
	}
	return *this;
}

void SparseRowIndex::insert(std::uint32_t id, std::size_t row) {
	if (row >= std::numeric_limits<std::uint32_t>::max()) {
		throw std::length_error("Sparse component row exceeds index capacity");
	}
	const auto pageIndex = id >> pageBits;
	const auto directoryIndex = pageIndex >> directoryBits;
	if (directoryIndex >= mDirectories.size()) {
		mDirectories.resize(directoryIndex + 1);
	}
	auto& directory = mDirectories[directoryIndex];
	if (!directory) {
		directory = std::make_unique<Directory>();
	}
	auto& page = directory->pages[pageIndex & (directorySize - 1)];
	if (!page) {
		page = std::make_unique<Page>();
		++directory->live;
	}
	auto& slot = page->rows[id & (pageSize - 1)];
	if (slot) {
		throw std::logic_error("Component added to the same entity twice");
	}
	slot = static_cast<std::uint32_t>(row) + 1;
	++page->live;
}

void SparseRowIndex::setRow(std::uint32_t id, std::size_t row) noexcept {
	assert(find(id) != missing && row < std::numeric_limits<std::uint32_t>::max());
	const auto pageIndex = id >> pageBits;
	auto& page = mDirectories[pageIndex >> directoryBits]->pages[pageIndex & (directorySize - 1)];
	page->rows[id & (pageSize - 1)] = static_cast<std::uint32_t>(row) + 1;
}

void SparseRowIndex::erase(std::uint32_t id) noexcept {
	assert(find(id) != missing);
	const auto pageIndex = id >> pageBits;
	auto& directory = mDirectories[pageIndex >> directoryBits];
	auto& page = directory->pages[pageIndex & (directorySize - 1)];
	page->rows[id & (pageSize - 1)] = 0;
	if (--page->live == 0) {
		page.reset();
		if (--directory->live == 0) {
			directory.reset();
		}
	}
}
}
