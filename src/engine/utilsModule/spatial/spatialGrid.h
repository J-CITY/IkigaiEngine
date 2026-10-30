#pragma once
#include "mathModule/geometry/aabb.h"
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace IKIGAI::UTILS::SPATIAL {
template <std::size_t N, class Payload = std::uint64_t> class SpatialGrid {
public:
	using Bounds = MATH::GEOMETRY::Aabb<N>;

private:
	using Cell = std::array<int, N>;
	float mSize;
	std::map<Cell, std::vector<Payload>> mCells;
	std::map<Payload, Bounds> mEntries;
	std::vector<Payload> mLarge;
	Cell cell(const std::array<float, N>& p) const {
		Cell result;
		for (std::size_t i = 0; i < N; ++i) {
			auto c = std::floor(double(p[i]) / mSize);
			if (!std::isfinite(c) || c < std::numeric_limits<int>::min() + 1.0 || c > std::numeric_limits<int>::max() - 1.0)
				throw std::invalid_argument("SpatialGrid coordinate out of range");
			result[i] = static_cast<int>(c);
		}
		return result;
	}
	static bool large(const Cell& a, const Cell& b) {
		double count = 1;
		for (std::size_t i = 0; i < N; ++i) {
			count *= double(b[i]) - a[i] + 1;
			if (count > 4096)
				return true;
		}
		return false;
	}
	template <class F> static void cells(const Cell& a, const Cell& b, F f) {
		Cell c = a;
		for (;;) {
			f(c);
			std::size_t i = 0;
			for (; i < N; ++i) {
				if (c[i] < b[i]) {
					++c[i];
					break;
				}
				c[i] = a[i];
			}
			if (i == N)
				break;
		}
	}
	void rebuild() {
		mCells.clear();
		mLarge.clear();
		for (const auto& [id, b] : mEntries)
			place(id, b);
	}
	void place(const Payload& id, const Bounds& b) {
		auto low = cell(b.min), high = cell(b.max);
		if (large(low, high))
			mLarge.push_back(id);
		else
			cells(low, high, [&](auto c) { mCells[c].push_back(id); });
	}

public:
	explicit SpatialGrid(float size = 2)
		: mSize(size) {
		if (!std::isfinite(size) || size <= 0)
			throw std::invalid_argument("Grid cell size must be positive");
	}
	void clear() {
		mEntries.clear();
		mCells.clear();
		mLarge.clear();
	}
	void reset(float size) {
		SpatialGrid next(size);
		*this = std::move(next);
	}
	void upsert(Payload id, const Bounds& bounds) {
		bounds.validate();
		cell(bounds.min);
		cell(bounds.max);
		bool existing = mEntries.contains(id);
		mEntries.insert_or_assign(id, bounds);
		if (existing)
			rebuild();
		else
			place(id, bounds);
	}
	bool remove(Payload id) {
		if (!mEntries.erase(id))
			return false;
		rebuild();
		return true;
	}
	void query(const Bounds& bounds, std::vector<Payload>& out) const {
		bounds.validate();
		auto low = cell(bounds.min), high = cell(bounds.max);
		if (large(low, high)) {
			for (const auto& [id, b] : mEntries)
				out.push_back(id);
		}
		else {
			out.insert(out.end(), mLarge.begin(), mLarge.end());
			cells(low, high, [&](auto c) {
				if (auto it = mCells.find(c); it != mCells.end())
					out.insert(out.end(), it->second.begin(), it->second.end());
			});
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
	}
	void pairs(std::vector<std::pair<Payload, Payload>>& out) const {
		std::vector<std::pair<Payload, Payload>> result;
		auto add = [&](auto a, auto b) {
			if (a != b) {
				result.emplace_back(std::min(a, b), std::max(a, b));
			}
		};
		for (const auto& [c, ids] : mCells) {
			for (std::size_t a = 0; a < ids.size(); ++a) {
				for (std::size_t b = a + 1; b < ids.size(); ++b) {
					add(ids[a], ids[b]);
				}
			}
		}
		for (auto a : mLarge) {
			for (const auto& [b, bounds] : mEntries) {
				add(a, b);
			}
		}
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		out.insert(out.end(), result.begin(), result.end());
	}
};
}
