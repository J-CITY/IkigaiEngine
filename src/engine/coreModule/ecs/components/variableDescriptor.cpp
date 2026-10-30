#include "variableDescriptor.h"
#include <stdexcept>

namespace IKIGAI::ECS {
DATA::ValueType VariableDescriptor::valueType() const {
	return Type == VariableType::Event	  ? DATA::ValueType::Bool
		: Type == VariableType::EntityRef ? DATA::ValueType::EntityRef
										  : static_cast<DATA::ValueType>(Type);
}
DATA::Value VariableDescriptor::initialValue() const {
	const auto type = valueType();
	if (Default) {
		if (Default->Type != type) {
			throw std::runtime_error("Variable default does not match its declared type");
		}
		return Default->value();
	}
	switch (type) {
	case DATA::ValueType::Float:
		return 0.f;
	case DATA::ValueType::Int:
		return 0;
	case DATA::ValueType::Bool:
		return false;
	case DATA::ValueType::Vector3:
		return MATH::Vector3f {};
	case DATA::ValueType::EntityRef:
		return DATA::EntityRef {};
	}
	throw std::runtime_error("Invalid variable type");
}
}
