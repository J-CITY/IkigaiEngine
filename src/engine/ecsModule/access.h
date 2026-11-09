#pragma once
#include <type_traits>

namespace IKIGAI::ECS {
template <typename T> struct Read {
	using Type = T;
};
template <typename T> struct Write {
	using Type = T;
};
template <typename T> struct Exclude {
	using Type = T;
};

template <typename T> inline constexpr bool IsExclude = false;
template <typename T> inline constexpr bool IsExclude<Exclude<T>> = true;

template <typename T> struct AccessTraits {
	using Type = std::remove_cvref_t<T>;
	static constexpr bool ReadOnly = false;
};

template <typename T> struct AccessTraits<const T> {
	using Type = std::remove_cvref_t<T>;
	static constexpr bool ReadOnly = true;
};

template <typename T> struct AccessTraits<Read<T>> {
	using Type = typename AccessTraits<T>::Type;
	static constexpr bool ReadOnly = true;
};

template <typename T> struct AccessTraits<Write<T>> {
	using Type = typename AccessTraits<T>::Type;
	static constexpr bool ReadOnly = false;
};

// Helper to get raw type
template <typename T> using RawType = typename AccessTraits<T>::Type;

template <typename T> using QueryReference = std::conditional_t<AccessTraits<T>::ReadOnly, const RawType<T>&, RawType<T>&>;
}
