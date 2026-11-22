#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace IKIGAI::DATA {
// serdepp delegates numeric conversion to nlohmann, which permits coercion and narrowing.
template <class Error = std::runtime_error> struct JsonIntegerRange {
	uint64_t minimum;
	uint64_t maximum;
	const char* message;

	template <class T, class Context, class Next> void from(Context& context, T& member, std::string_view name, Next&& next) const {
		const auto& value = context.adaptor.at(std::string(name));
		if (!value.is_number_integer() || (!value.is_number_unsigned() && value.template get<int64_t>() < 0)) {
			throw Error(message);
		}
		const auto number = value.template get<uint64_t>();
		if (number < minimum || number > maximum) {
			throw Error(message);
		}
		next.from(context, member, name);
	}

	template <class T, class Context, class Next> void into(Context& context, T& member, std::string_view name, Next&& next) const {
		next.into(context, member, name);
	}
};

// serdepp iterates any JSON value and emits null for an empty collection.
struct JsonContainer {
	nlohmann::json::value_t type;
	std::size_t minimumSize = 0;
	std::size_t maximumSize = std::numeric_limits<std::size_t>::max();

	template <class T, class Context, class Next> void from(Context& context, T& member, std::string_view name, Next&& next) const {
		const auto& value = context.adaptor.at(std::string(name));
		if (value.type() != type || value.size() < minimumSize || value.size() > maximumSize) {
			throw std::runtime_error("Invalid JSON collection: " + std::string(name));
		}
		next.from(context, member, name);
	}

	template <class T, class Context, class Next> void into(Context& context, T& member, std::string_view name, Next&& next) const {
		context.adaptor[std::string(name)] = nlohmann::json(type);
		next.into(context, member, name);
	}
};
}
