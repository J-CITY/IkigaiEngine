#pragma once
#include <nlohmann/json.hpp>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/serde.hpp>
#include <stdexcept>
namespace IKIGAI::DATA {
void validateDescriptorShape(const nlohmann::json&, const nlohmann::json&, const std::string& path = {});
// JSON is confined to the authoring boundary. Registered extensions use this
// same typed boundary; their compiled callbacks never interpret JSON.
template <class T> T readDescriptor(nlohmann::json document) {
	if (!document.is_object()) {
		throw std::runtime_error("Expected descriptor object");
	}
	auto result = serde::deserialize<T>(document);
	auto shape = serde::serialize<nlohmann::json>(result);
	validateDescriptorShape(document, shape);
	return result;
}
}
