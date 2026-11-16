#pragma once
#include <atomic>
#include <compare>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace IKIGAI {
// Zero is valid; max() is the invalid sentinel. Generation is a separate service.
template <class Tag, std::unsigned_integral Rep = std::uint32_t> class Id {
	static_assert(!std::same_as<Rep, bool>);

public:
	using value_type = Rep;
	constexpr Id() noexcept = default;
	explicit constexpr Id(Rep value) noexcept
		: mValue(value) { }
	template <std::integral U>
		requires(!std::same_as<U, Rep> && !std::same_as<U, bool>)
	explicit constexpr Id(U value) {
		if (!std::in_range<Rep>(value)) {
			throw std::out_of_range("ID value out of range");
		}
		mValue = static_cast<Rep>(value);
	}
	static constexpr Id invalid() noexcept {
		return {};
	}
	constexpr bool valid() const noexcept {
		return mValue != std::numeric_limits<Rep>::max();
	}
	constexpr Rep value() const noexcept {
		return mValue;
	}
	template <std::integral U>
		requires(!std::same_as<U, bool>)
	explicit constexpr operator U() const {
		if (!std::in_range<U>(mValue)) {
			throw std::out_of_range("ID conversion out of range");
		}
		return static_cast<U>(mValue);
	}
	constexpr auto operator<=>(const Id&) const noexcept = default;

private:
	Rep mValue = std::numeric_limits<Rep>::max();
};

// One-past-last fits in Rep. Observing a loaded value does not detect duplicates.
template <class Tag, std::unsigned_integral Rep = std::uint32_t, Rep First = 0, Rep Last = std::numeric_limits<Rep>::max() - 1> class IdSequence {
	static_assert(First <= Last && Last < std::numeric_limits<Rep>::max());

public:
	using ID = Id<Tag, Rep>;
	ID next() {
		return nextRange(1);
	}
	// Reserve consecutive IDs with one atomic update and return the first.
	ID nextRange(Rep count) {
		if (count == 0) {
			throw std::invalid_argument("ID range must be nonempty");
		}
		auto value = mNext.load(std::memory_order_relaxed);
		for (;;) {
			if (value > Last || count - 1 > Last - value) {
				throw std::overflow_error("ID sequence exhausted");
			}
			if (mNext.compare_exchange_weak(value, static_cast<Rep>(value + count), std::memory_order_relaxed)) {
				return ID(value);
			}
		}
	}
	void observeLoaded(ID id) {
		if (!id.valid() || id.value() > Last) {
			throw std::out_of_range("Loaded ID out of range");
		}
		if (id.value() < First) {
			return; // e.g. the legacy entity-zero parent sentinel
		}
		const auto next = static_cast<Rep>(id.value() + 1);
		auto value = mNext.load(std::memory_order_relaxed);
		while (value < next && !mNext.compare_exchange_weak(value, next, std::memory_order_relaxed)) { }
	}

private:
	std::atomic<Rep> mNext { First };
};

template <class Tag> struct IdDomain {
	static constexpr std::uint32_t first = 0;
	// Legacy engine users serialize/convert these IDs through signed int.
	static constexpr std::uint32_t last = std::numeric_limits<int>::max();
};

template <class T> class IdGenerator {
public:
	using ID = Id<T>;
	IdGenerator()
		: mId(generateId()) { }
	explicit IdGenerator(ID id)
		: mId(id) {
		sequence().observeLoaded(id);
	}
	ID getUniqueId() const noexcept {
		return mId;
	}
	static ID generateId() {
		return sequence().next();
	}

protected:
	ID mId;

private:
	static auto& sequence() {
		static IdSequence<T, std::uint32_t, IdDomain<T>::first, IdDomain<T>::last> ids;
		return ids;
	}
};
}
namespace std {
template <class Tag, std::unsigned_integral Rep> struct hash<IKIGAI::Id<Tag, Rep>> {
	size_t operator()(IKIGAI::Id<Tag, Rep> id) const noexcept {
		return hash<Rep> {}(id.value());
	}
};
}
