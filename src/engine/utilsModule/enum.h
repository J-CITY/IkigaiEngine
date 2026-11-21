#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <string_view>
#include <type_traits>


namespace IKIGAI::UTILS {
template <class Ty> inline constexpr bool isEnum = false;

namespace Impl {

	template <class Ty> inline constexpr Ty Max = Ty { };

	template <class Ty> inline constexpr Ty Tokens = Ty { };

	using sv = std::string_view;

	consteval auto trim(sv str) {
		auto is_space = [](auto ch) { return ch == ' ' || ch == ',' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\v'; };
		while (is_space(str.front()))
			str = str.substr(1);
		while (is_space(str.back()))
			str = str.substr(0, str.size() - 1);
		return str;
	};

	template <class E> consteval auto to_num(sv str) {
		std::underlying_type_t<E> val { };
		for (auto var : str) {
			if (var == '-')
				continue;
			val *= 10, val += var - '0';
		}
		return str.starts_with('-') ? -val : val;
	};

	consteval size_t enum_size(sv enums) {
		return std::ranges::count(enums, ',') + !enums.ends_with(',');
	}

	template <size_t N, class T> consteval auto tokenize_enum(sv base) {
		size_t count { };
		std::array<std::pair<sv, T>, N> tokens;
		std::underlying_type_t<T> val { 0 };
		sv name;
		for (auto&& word : std::ranges::views::split(base, std::string_view(","))) {
			for (int i { }; auto&& tok : std::ranges::views::split(word, std::string_view("="))) {
				sv token { tok.begin(), tok.end() };
				if (!i)
					name = trim(token);
				else if (token.size())
					val = to_num<T>(trim(token));
				++i;
			}
			tokens[count++] = { name, static_cast<T>(val) };
			++val;
		}
		return tokens;
	}
}

template <class E>
	requires isEnum<E>
constexpr const auto& enumEntries() {
	return Impl::Tokens<E>;
}

template <class E>
	requires isEnum<E>
constexpr auto enumToString(E e) {
	auto it = std::ranges::find(Impl::Tokens<E>, e, &std::pair<Impl::sv, E>::second);
	return it == Impl::Tokens<E>.end() ? Impl::sv { } : it->first;
}

template <class E> constexpr Impl::sv enumToString(E e) {
	return { };
}

template <class E>
	requires isEnum<E>
constexpr E stringToEnum(Impl::sv str) {
	auto it = std::ranges::find(Impl::Tokens<E>, str, &std::pair<Impl::sv, E>::first);
	return it == Impl::Tokens<E>.end() ? static_cast<E>(std::numeric_limits<std::underlying_type_t<E>>::min()) : it->second;
}

template <class E> constexpr E stringToEnum(Impl::sv str) {
	return { };
}
}

#define IKIGAI_ENUM(T, ...)                                                                                                                          \
	enum class T : int { __VA_ARGS__ };                                                                                                              \
	template <> inline constexpr bool IKIGAI::UTILS::isEnum<T> = true;                                                                               \
	template <> inline constexpr auto IKIGAI::UTILS::Impl::Max<T> = IKIGAI::UTILS::Impl::enum_size(#__VA_ARGS__);                                    \
	template <>                                                                                                                                      \
	inline constexpr auto IKIGAI::UTILS::Impl::Tokens<T> = IKIGAI::UTILS::Impl::tokenize_enum<IKIGAI::UTILS::Impl::Max<T>, T>(#__VA_ARGS__);

#define IKIGAI_ENUM_NS(NS, T, ...)                                                                                                                   \
	namespace NS {                                                                                                                                   \
		enum class T : int { __VA_ARGS__ };                                                                                                          \
	}                                                                                                                                                \
	template <> inline constexpr bool IKIGAI::UTILS::isEnum<NS::T> = true;                                                                           \
	template <> inline constexpr auto IKIGAI::UTILS::Impl::Max<NS::T> = IKIGAI::UTILS::Impl::enum_size(#__VA_ARGS__);                                \
	template <>                                                                                                                                      \
	inline constexpr auto IKIGAI::UTILS::Impl::Tokens<NS::T>                                                                                         \
		= IKIGAI::UTILS::Impl::tokenize_enum<IKIGAI::UTILS::Impl::Max<NS::T>, NS::T>(#__VA_ARGS__);

#define DEFINE_ENUM_CLASS_BITWISE_OPERATORS(Enum)                                                                                                    \
	inline constexpr Enum operator|(Enum Lhs, Enum Rhs) {                                                                                            \
		return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(Lhs) | static_cast<std::underlying_type_t<Enum>>(Rhs));                   \
	}                                                                                                                                                \
	inline constexpr Enum operator&(Enum Lhs, Enum Rhs) {                                                                                            \
		return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(Lhs) & static_cast<std::underlying_type_t<Enum>>(Rhs));                   \
	}                                                                                                                                                \
	inline constexpr Enum operator^(Enum Lhs, Enum Rhs) {                                                                                            \
		return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(Lhs) ^ static_cast<std::underlying_type_t<Enum>>(Rhs));                   \
	}                                                                                                                                                \
	inline constexpr Enum operator~(Enum E) {                                                                                                        \
		return static_cast<Enum>(~static_cast<std::underlying_type_t<Enum>>(E));                                                                     \
	}                                                                                                                                                \
	inline Enum& operator|=(Enum& Lhs, Enum Rhs) {                                                                                                   \
		return Lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(Lhs) | static_cast<std::underlying_type_t<Enum>>(Lhs));             \
	}                                                                                                                                                \
	inline Enum& operator&=(Enum& Lhs, Enum Rhs) {                                                                                                   \
		return Lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(Lhs) & static_cast<std::underlying_type_t<Enum>>(Lhs));             \
	}                                                                                                                                                \
	inline Enum& operator^=(Enum& Lhs, Enum Rhs) {                                                                                                   \
		return Lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(Lhs) ^ static_cast<std::underlying_type_t<Enum>>(Lhs));             \
	}
