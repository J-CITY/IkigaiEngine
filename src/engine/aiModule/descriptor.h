#pragma once
#include "utilsModule/serialization/typedDescriptor.h"
#include <nlohmann/json.hpp>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/serde.hpp>
#include <stdexcept>

namespace IKIGAI::AI {
struct AuthoringParameters {
	nlohmann::json document = nlohmann::json::object();
};
}
namespace serde {
// nlohmann::json is iterable; passing it directly through serdepp would treat
// scalar leaves as sequences. Preserve this extension payload only at load time.
template <> struct serde_adaptor<nlohmann::json, IKIGAI::AI::AuthoringParameters> {
	static void from(nlohmann::json& json, std::string_view key, IKIGAI::AI::AuthoringParameters& value) {
		value.document = key.empty() ? json : json.at(std::string(key));
		if (!value.document.is_object()) {
			throw std::runtime_error("Node Parameters must be an object");
		}
	}
	static void into(nlohmann::json& json, std::string_view key, const IKIGAI::AI::AuthoringParameters& value) {
		(key.empty() ? json : json[std::string(key)]) = value.document;
	}
};
}
namespace IKIGAI::AI {
using DATA::readDescriptor;
using DATA::validateDescriptorShape;
}
