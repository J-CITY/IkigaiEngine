#pragma once

#include "jsonError.h"
#include "utilsModule/result.h"
#include "utilsModule/serialization/jsonAdapters.h"
#include <exception>
#include <nlohmann/json.hpp>
#include <serdepp/serializer.hpp>

namespace IKIGAI::UTILS {
struct JsonOk { };
}

namespace IKIGAI::DATA {
// The caller supplies the resolved document. The source path is diagnostic context.
template <class T> UTILS::Result<T, UTILS::JsonError> DeserializeJson(nlohmann::json& data, const std::string& sourcePath = {}) {
	try {
		return UTILS::Ok(serde::deserialize<T>(data));
	} catch (const std::exception& error) {
		return UTILS::Err(UTILS::JsonError(UTILS::JsonError::Kind::PARSE, error.what(), sourcePath));
	}
}

template <class T> UTILS::Result<UTILS::JsonOk, UTILS::JsonError> DeserializeJson(T& value, nlohmann::json& data) {
	try {
		value = serde::deserialize<T>(data);
		return UTILS::Ok(UTILS::JsonOk {});
	} catch (const std::exception& error) {
		return UTILS::Err(UTILS::JsonError(UTILS::JsonError::Kind::PARSE, error.what()));
	}
}
}
