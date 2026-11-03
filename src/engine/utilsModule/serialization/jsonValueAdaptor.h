#pragma once

#include <nlohmann/json.hpp>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <string_view>

// nlohmann::json is iterable, so serdepp classifies it as a sequence and the generic adapter recurses
// forever when a descriptor stores raw JSON values (std::vector<nlohmann::json>). A JSON value is already
// in its serialized form: copy it through.
namespace serde {
template <> struct serde_adaptor<nlohmann::json, nlohmann::json, type::seq_t> {
	static void from(nlohmann::json& source, std::string_view key, nlohmann::json& value) {
		value = key.empty() ? source : source.at(std::string(key));
	}

	static void into(nlohmann::json& target, std::string_view key, const nlohmann::json& value) {
		(key.empty() ? target : target[std::string(key)]) = value;
	}
};
}
