#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace IKIGAI::ECS {
class SparseRowIndex {
	static constexpr unsigned pageBits = 10;
	static constexpr unsigned directoryBits = 10;
	static constexpr std::size_t pageSize = std::size_t { 1 } << pageBits;
	static constexpr std::size_t directorySize = std::size_t { 1 } << directoryBits;

	// Zero denotes a missing ID. Occupied slots store the dense row plus one.
	struct Page {
		std::array<std::uint32_t, pageSize> rows {};
		std::size_t live = 0;
	};

	struct Directory {
		std::array<std::unique_ptr<Page>, directorySize> pages {};
		std::size_t live = 0;
	};

	std::vector<std::unique_ptr<Directory>> mDirectories;

public:
	static constexpr std::size_t missing = static_cast<std::size_t>(-1);

	SparseRowIndex() = default;
	SparseRowIndex(const SparseRowIndex& other);
	SparseRowIndex(SparseRowIndex&&) noexcept = default;
	SparseRowIndex& operator=(const SparseRowIndex& other);
	SparseRowIndex& operator=(SparseRowIndex&&) noexcept = default;

	[[nodiscard]] std::size_t find(std::uint32_t id) const noexcept {
		const auto pageIndex = id >> pageBits;
		const auto directoryIndex = pageIndex >> directoryBits;
		if (directoryIndex >= mDirectories.size() || !mDirectories[directoryIndex]) {
			return missing;
		}
		const auto& page = mDirectories[directoryIndex]->pages[pageIndex & (directorySize - 1)];
		return page ? static_cast<std::size_t>(page->rows[id & (pageSize - 1)]) - 1 : missing;
	}

	void insert(std::uint32_t id, std::size_t row);
	void setRow(std::uint32_t id, std::size_t row) noexcept;
	void erase(std::uint32_t id) noexcept;
};
}
