#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <serdepp/serializer.hpp>
#include <nlohmann/json.hpp>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include "result.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/fileSystem/fileSystem.h"

namespace IKIGAI::UTILS {
	static const std::string PARENT_KEY = "##parent";
	static const std::string PATCH_KEY = "##patch";

	struct JsonError {
		enum class Kind {
			PARSE,
			FILE_NOT_EXIST,
			SEREALIZE
		};
		JsonError() = default;
		JsonError(Kind kind, std::string&& text) : kind(kind), text(std::move(text)) {}
		Kind kind;
		std::string text;
	};

	struct JsonOk {};

	static Result<std::string, JsonError> ReadFileIntoString(const std::string& path) {
		auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
		auto file = fs.getFile(path);
		if (!file || !file->isValid()) {
			return Err(JsonError(JsonError::Kind::FILE_NOT_EXIST, "Can not open file: " + path));
		}
		return Ok(file->readStr());
	}

	static Result<nlohmann::json, JsonError> ApplyPatches(const nlohmann::json& data) {
		const auto parentIt = data.find(PARENT_KEY);
		if (parentIt == data.end()) {
			return Ok(data);
		}
		const auto patchIt = data.find(PATCH_KEY);
		if (patchIt == data.end()) {
			return Err(JsonError(JsonError::Kind::SEREALIZE, "No patch in config"));
		}
		try {
			const auto parentJsonStr = ReadFileIntoString(parentIt.value());
			auto parentJson = nlohmann::json::parse(parentJsonStr.unwrap(), nullptr, true, true);
			auto result = ApplyPatches(parentJson).unwrap().patch(*patchIt);
			result[PARENT_KEY] = parentIt.value();
			return Ok(result);
		} catch (std::exception& e) {
			return Err(JsonError(JsonError::Kind::SEREALIZE, "Problem"));
		}
	}

	static Result<nlohmann::json, JsonError> GetPatches(const nlohmann::json& data) {
		const auto parentIt = data.find(PARENT_KEY);
		if (parentIt == data.end()) {
			return Ok(data);
		}
		try {
			const auto parentJsonStr = ReadFileIntoString(parentIt.value());
			auto parentJson = nlohmann::json::parse(parentJsonStr.unwrap(), nullptr, true, true);
			parentJson = ApplyPatches(parentJson).unwrap();

			auto dataForDiff = data;
			dataForDiff.erase(PARENT_KEY);

			nlohmann::json result;
			result[PARENT_KEY] = parentIt.value();
			result[PATCH_KEY] = nlohmann::json::diff(parentJson, dataForDiff);
			return Ok(result);
		}
		catch (std::exception& e) {
			return Err(JsonError(JsonError::Kind::SEREALIZE, "Problem"));
		}
	}

	template<class T>
	Result<T, JsonError> FromJson(nlohmann::json& data) {
		try {
			data = ApplyPatches(data).unwrap();
			auto obj = serde::deserialize<T>(data);
			return Ok(obj);
		} catch (std::exception& e) {
			return Err(JsonError(JsonError::Kind::PARSE, e.what()));
		}
	}

	template<class T>
	Result<JsonOk, JsonError> FromJson(T& obj, nlohmann::json& data) {
		try {
			data = ApplyPatches(data).unwrap();
			obj = serde::deserialize<T>(data);
			return Ok(JsonOk());
		} catch (std::exception& e) {
			return Err(JsonError(JsonError::Kind::PARSE, e.what()));
		}
	}

	template<class T>
	Result<T, JsonError> FromJsonStr(const std::string& jsonStr) {
		nlohmann::json data;
		try {
			data = nlohmann::json::parse(jsonStr, nullptr, true, true);
		} catch (const std::exception& e) {
			return Err(JsonError(JsonError::Kind::PARSE, e.what()));
		}
		return FromJson<T>(data);
	}

	template<class T>
	Result<T, JsonError> FromJson(const std::string& path) {
		auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
		auto file = fs.getFile(path);
		if (!file || !file->isValid()) {
			return Err(JsonError(JsonError::Kind::FILE_NOT_EXIST, "Can not open file: " + path));
		}
		return FromJsonStr<T>(file->readStr());
	}

	template<class T>
	Result<nlohmann::json, JsonError> ToJson(const T& obj) {
		try {
			nlohmann::json jsonData = serde::serialize<nlohmann::json>(obj);
			auto patch = GetPatches(jsonData);

			return Ok(patch.unwrap());
		} catch (std::exception& e) {
			return Err(JsonError(JsonError::Kind::SEREALIZE, e.what()));
		}
	}

	template<class T>
	Result<std::string, JsonError> ToJsonStr(const T& obj, int tab=-1) {
		auto res = ToJson<T>(obj);
		if (res.isErr()) {
			return Err(res.unwrapErr());
		}
		return Ok(res.unwrap().dump(tab));
	}
}
