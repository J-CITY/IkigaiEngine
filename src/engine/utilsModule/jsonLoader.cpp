#include "jsonLoader.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace IKIGAI::UTILS {
JsonError::JsonError(Kind kind, std::string text, std::string path, std::vector<std::string> inheritanceChain)
	: kind(kind)
	, text(std::move(text))
	, path(std::move(path))
	, inheritanceChain(std::move(inheritanceChain)) {
	if (!this->path.empty()) {
		this->text += " [" + this->path + "]";
	}
	if (this->inheritanceChain.size() > 1) {
		this->text += " (##parent chain: " + this->inheritanceChain.front();
		for (std::size_t index = 1; index < this->inheritanceChain.size(); ++index) {
			this->text += " -> " + this->inheritanceChain[index];
		}
		this->text += ")";
	}
}

namespace {
	using JsonResult = Result<nlohmann::json, JsonError>;
	using ErrorKind = JsonError::Kind;
	constexpr std::size_t MaxActiveJsonFiles = 64;

	enum class PatchOperation { Apply, Generate };

	class JsonParentResolver {
		std::vector<std::string> mActivePaths;
		std::vector<std::string>* mParentDependencies;

		JsonError error(ErrorKind kind, std::string text) const {
			return { kind, std::move(text), mActivePaths.empty() ? std::string {} : mActivePaths.back(), mActivePaths };
		}

		JsonError pathError(ErrorKind kind, std::string text, const std::string& path) const {
			auto chain = mActivePaths;
			chain.push_back(path);
			return { kind, std::move(text), path, std::move(chain) };
		}

		Result<std::string, JsonError> normalize(const std::string& path) const {
			try {
				return Ok(RESOURCES::FileSystem::normalizePath(path));
			} catch (const std::exception& e) {
				return Err(pathError(ErrorKind::INVALID_PATH, e.what(), path));
			}
		}

		Result<std::string, JsonError> readText(const std::string& path) const {
			try {
				auto& filesystem = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
				auto file = filesystem.getFile(path);
				if (!file || !file->isValid()) {
					return Err(error(ErrorKind::FILE_NOT_EXIST, "Cannot open JSON file"));
				}
				return Ok(file->readStr());
			} catch (const std::exception& e) {
				return Err(error(ErrorKind::FILE_READ, e.what()));
			}
		}

	public:
		explicit JsonParentResolver(std::vector<std::string>* parentDependencies = nullptr)
			: mParentDependencies(parentDependencies) {
			if (mParentDependencies) {
				mParentDependencies->clear();
			}
		}

		std::optional<JsonError> setSourcePath(const std::string& sourcePath) {
			if (!sourcePath.empty()) {
				auto normalized = normalize(sourcePath);
				if (normalized.isErr()) {
					return normalized.unwrapErr();
				}
				mActivePaths.push_back(normalized.unwrap());
			}
			return {};
		}

		JsonResult read(const std::string& path, std::vector<std::string>* dependencyPaths = nullptr) {
			auto normalized = normalize(path);
			if (normalized.isErr()) {
				return Err(normalized.unwrapErr());
			}
			const auto key = normalized.unwrap();
			if (std::ranges::find(mActivePaths, key) != mActivePaths.end()) {
				return Err(pathError(ErrorKind::PARENT_CYCLE, "Cycle in ##parent inheritance", key));
			}
			if (mActivePaths.size() >= MaxActiveJsonFiles) {
				return Err(
					pathError(ErrorKind::PARENT_DEPTH, "##parent inheritance exceeds " + std::to_string(MaxActiveJsonFiles) + " active files", key));
			}
			mActivePaths.push_back(key);
			if (dependencyPaths) {
				dependencyPaths->push_back(key);
			}
			auto text = readText(key);
			auto resolved = text.isOk() ? parse(text.unwrap()) : JsonResult(Err(text.unwrapErr()));
			mActivePaths.pop_back();
			return resolved;
		}

		JsonResult parse(const std::string& text) {
			nlohmann::json data;
			try {
				data = nlohmann::json::parse(text, nullptr, true, true);
			} catch (const std::exception& e) {
				return Err(error(ErrorKind::PARSE, e.what()));
			}
			return process(data, PatchOperation::Apply);
		}

		JsonResult process(const nlohmann::json& data, PatchOperation operation) {
			const auto parent = data.find(PARENT_KEY);
			if (parent == data.end()) {
				return Ok(data);
			}
			if (!parent->is_string()) {
				return Err(error(ErrorKind::INVALID_PARENT, "##parent must be a string"));
			}
			const auto& parentPath = parent->get_ref<const std::string&>();
			const auto patch = data.find(PATCH_KEY);
			if (parentPath.empty()) {
				if (operation == PatchOperation::Apply && patch != data.end()) {
					return Err(error(ErrorKind::SEREALIZE, "Patch requires a nonempty parent"));
				}
				auto plain = data;
				plain.erase(PARENT_KEY);
				return Ok(plain);
			}
			if (operation == PatchOperation::Apply && patch == data.end()) {
				return Err(error(ErrorKind::SEREALIZE, "No patch in config"));
			}
			auto resolvedParent = read(parentPath, mParentDependencies);
			if (resolvedParent.isErr()) {
				return Err(resolvedParent.unwrapErr());
			}
			try {
				if (operation == PatchOperation::Generate) {
					auto dataForDiff = data;
					dataForDiff.erase(PARENT_KEY);
					return Ok(nlohmann::json {
						{ PARENT_KEY, parentPath },
						{ PATCH_KEY, nlohmann::json::diff(resolvedParent.unwrap(), dataForDiff) },
					});
				}
				auto result = resolvedParent.unwrap().patch(*patch);
				result[PARENT_KEY] = parentPath;
				return Ok(result);
			} catch (const std::exception& e) {
				return Err(error(ErrorKind::SEREALIZE, "##patch against " + parentPath + ": " + e.what()));
			}
		}
	};

	JsonResult ProcessPatches(const nlohmann::json& data, const std::string& sourcePath, PatchOperation operation) {
		JsonParentResolver resolver;
		if (auto error = resolver.setSourcePath(sourcePath)) {
			return Err(*error);
		}
		return resolver.process(data, operation);
	}
}

Result<nlohmann::json, JsonError> ApplyPatches(const nlohmann::json& data, const std::string& sourcePath) {
	return ProcessPatches(data, sourcePath, PatchOperation::Apply);
}

Result<nlohmann::json, JsonError> GetPatches(const nlohmann::json& data, const std::string& sourcePath) {
	return ProcessPatches(data, sourcePath, PatchOperation::Generate);
}

namespace JsonDetail {
	Result<nlohmann::json, JsonError> ReadResolvedJson(const std::string& path, std::vector<std::string>* parentDependencies) {
		return JsonParentResolver(parentDependencies).read(path);
	}

	Result<nlohmann::json, JsonError> ParseResolvedJson(
		const std::string& text, const std::string& sourcePath, std::vector<std::string>* parentDependencies) {
		JsonParentResolver resolver(parentDependencies);
		if (auto error = resolver.setSourcePath(sourcePath)) {
			return Err(*error);
		}
		return resolver.parse(text);
	}

	void ValidateFeatures(const nlohmann::json& data) {
		SCRIPTING::ValidateScriptFeatures(data);
		PHYSICS2D::validateFeatures(data);
		PHYSICS::validateFeatures(data);
	}
}
}
