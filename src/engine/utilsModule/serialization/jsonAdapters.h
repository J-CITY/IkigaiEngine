#pragma once

#include "mathModule/curves.h"
#include <array>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/default.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace serde {
template <typename E, size_t SIZE> struct serde_adaptor<nlohmann::json, std::array<E, SIZE>, type::seq_t> {
	static void from(nlohmann::json& s, std::string_view key, std::array<E, SIZE>& arr) {
		auto& table = key.empty() ? s : s.at(std::string { key });
		if (!table.is_array() || table.size() != SIZE) {
			throw std::invalid_argument("Expected an array with exactly " + std::to_string(SIZE) + " elements");
		}
		int i = 0;
		for (auto& value : table) {
			arr[i] = std::move(deserialize<E>(value));
			++i;
		}
	}

	static void into(nlohmann::json& s, std::string_view key, const std::array<E, SIZE>& data) {
		nlohmann::json& arr = key.empty() ? s : s[std::string { key }];
		int i = 0;
		for (auto& value : data) {
			arr[i] = std::move(serialize<nlohmann::json>(value));
			++i;
		}
	}
};
}

namespace IKIGAI::MATH {
template <typename T> template <class Context> constexpr void Vector2<T>::serde(Context& context, Vector2& value) {
	using Self = Vector2;
	serde::serde_struct(context, value).field(&Self::x, "X").field(&Self::y, "Y");
}

template <typename T> template <class Context> constexpr void Vector3<T>::serde(Context& context, Vector3& value) {
	using Self = Vector3;
	serde::serde_struct(context, value).field(&Self::x, "X").field(&Self::y, "Y").field(&Self::z, "Z");
}

template <typename T> template <class Context> constexpr void Vector4<T>::serde(Context& context, Vector4& value) {
	using Self = Vector4;
	serde::serde_struct(context, value).field(&Self::x, "X").field(&Self::y, "Y").field(&Self::z, "Z").field(&Self::w, "W");
}

template <typename T> template <class Context> constexpr void Matrix3<T>::serde(Context& context, Matrix3& value) {
	using Self = Matrix3;
	serde::serde_struct(context, value).field(&Self::data, "Data");
}

template <typename T> template <class Context> constexpr void Matrix4<T>::serde(Context& context, Matrix4& value) {
	using Self = Matrix4;
	serde::serde_struct(context, value).field(&Self::data, "Data");
}

template <class Context> constexpr void CURVES::Curve::serde(Context& context, Curve& value) {
	using Self = Curve;
	serde::serde_struct(context, value)
		.field(&Self::mLeft, "Left")
		.field(&Self::mRight, "Right")
		.field(&Self::mLeftTangent, "LeftTangent")
		.field(&Self::mRightTangent, "RightTangent")
		.field(&Self::mLeftYOffset, "LeftYOffset");
}
}
