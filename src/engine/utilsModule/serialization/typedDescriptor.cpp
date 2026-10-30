#include "typedDescriptor.h"

namespace IKIGAI::DATA {
void validateDescriptorShape(const nlohmann::json& document, const nlohmann::json& shape, const std::string& path) {
	// serdepp emits empty containers as null. Preserve empty authored containers.
	if (shape.is_null() && (document.is_null() || (document.is_structured() && document.empty()))) {
		return;
	}
	const bool compatible = shape.is_number_integer() ? document.is_number_integer() && document == shape
		: shape.is_number_float()					  ? document.is_number()
													  : shape.type() == document.type();
	if (!compatible) {
		throw std::runtime_error("Descriptor field type mismatch: " + path);
	}
	if (document.is_object()) {
		for (const auto& [key, value] : document.items()) {
			if (!shape.contains(key)) {
				throw std::runtime_error("Unknown descriptor field: " + path + "/" + key);
			}
			validateDescriptorShape(value, shape.at(key), path + "/" + key);
		}
	}
	else if (document.is_array()) {
		if (document.size() != shape.size()) {
			throw std::runtime_error("Descriptor array mismatch: " + path);
		}
		for (size_t index = 0; index < document.size(); ++index) {
			validateDescriptorShape(document[index], shape[index], path + "/" + std::to_string(index));
		}
	}
}
}
