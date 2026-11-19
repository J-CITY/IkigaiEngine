#pragma once
#include "utilsModule/serialization/jsonSerde.h"
#include <exception>
#include <nlohmann/json.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

namespace IKIGAI::UTILS {
inline const std::string PARENT_KEY = "##parent";
inline const std::string PATCH_KEY = "##patch";

// Supply the VFS path when the caller already knows the document's origin.
Result<nlohmann::json, JsonError> ApplyPatches(const nlohmann::json& data, const std::string& sourcePath = {});
Result<nlohmann::json, JsonError> GetPatches(const nlohmann::json& data, const std::string& sourcePath = {});

namespace JsonDetail {
	Result<nlohmann::json, JsonError> ReadResolvedJson(const std::string& path, std::vector<std::string>* parentDependencies);
	Result<nlohmann::json, JsonError> ParseResolvedJson(
		const std::string& text, const std::string& sourcePath, std::vector<std::string>* parentDependencies);
}

template <class T> Result<T, JsonError> FromJson(nlohmann::json& data) {
	auto resolved = ApplyPatches(data);
	if (resolved.isErr()) {
		return Err(resolved.unwrapErr());
	}
	data = resolved.unwrap();
	return DATA::DeserializeJson<T>(data);
}

template <class T> Result<JsonOk, JsonError> FromJson(T& obj, nlohmann::json& data) {
	auto resolved = ApplyPatches(data);
	if (resolved.isErr()) {
		return Err(resolved.unwrapErr());
	}
	data = resolved.unwrap();
	return DATA::DeserializeJson(obj, data);
}

// The optional parentDependencies output is reset for each load and records normalized parent paths in traversal order.
// It excludes the source and retains valid missing paths so a caller can retry when they appear.
template <class T>
Result<T, JsonError> FromJsonStr(
	const std::string& jsonStr, const std::string& sourcePath = {}, std::vector<std::string>* parentDependencies = nullptr) {
	auto resolved = JsonDetail::ParseResolvedJson(jsonStr, sourcePath, parentDependencies);
	if (resolved.isErr()) {
		return Err(resolved.unwrapErr());
	}
	auto data = resolved.unwrap();
	return DATA::DeserializeJson<T>(data, sourcePath);
}

// parentDependencies has the same semantics as for FromJsonStr.
template <class T> Result<T, JsonError> FromJson(const std::string& path, std::vector<std::string>* parentDependencies = nullptr) {
	auto resolved = JsonDetail::ReadResolvedJson(path, parentDependencies);
	if (resolved.isErr()) {
		return Err(resolved.unwrapErr());
	}
	auto data = resolved.unwrap();
	return DATA::DeserializeJson<T>(data, path);
}

template <class T> Result<nlohmann::json, JsonError> ToJson(const T& obj, const std::string& sourcePath = {}) {
	try {
		nlohmann::json jsonData = serde::serialize<nlohmann::json>(obj);
		return GetPatches(jsonData, sourcePath);
	} catch (const std::exception& e) {
		return Err(JsonError(JsonError::Kind::SEREALIZE, e.what(), sourcePath));
	}
}

template <class T> Result<std::string, JsonError> ToJsonStr(const T& obj, int tab = -1, const std::string& sourcePath = {}) {
	auto res = ToJson<T>(obj, sourcePath);
	if (res.isErr()) {
		return Err(res.unwrapErr());
	}
	try {
		return Ok(res.unwrap().dump(tab));
	} catch (const std::exception& e) {
		return Err(JsonError(JsonError::Kind::SEREALIZE, e.what(), sourcePath));
	}
}
}
