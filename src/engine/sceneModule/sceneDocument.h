#pragma once
#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace IKIGAI::SCENE_SYSTEM::DOCUMENT {
using Json = nlohmann::json;
using IdMap = std::map<int, int>;
using ReadFn = std::function<Json(const std::string&)>;

// Scene metadata is read before GPU startup and by both scene loading paths.
std::string ReadRenderPipeline(const Json& document);
void WriteRenderPipeline(Json& document, const std::string& pipeline);

struct Instance {
	Json entry;
	// Default expanded objects (asset + nested prefabs, before instance Overrides).
	Json baseline = Json::array();
};

inline int Id(const Json& value) {
	if (!value.is_number_integer()) {
		throw std::runtime_error("Entity ID must be an integer");
	}
	const auto id = value.get<int64_t>();
	if (id <= 0 || id >= std::numeric_limits<int>::max()) {
		throw std::runtime_error("Entity ID out of range");
	}
	return static_cast<int>(id);
}

inline std::string Path(std::string path) {
	for (auto& c : path) {
		if (c == '\\') {
			c = '/';
		}
	}
	if (path.empty() || path.find(':') != std::string::npos) {
		throw std::runtime_error("Expected a VFS prefab path");
	}
	std::string result;
	std::size_t start = 0;
	while (start <= path.size()) {
		const auto end = path.find('/', start);
		const auto part = path.substr(start, end == std::string::npos ? end : end - start);
		if (part == "..") {
			throw std::runtime_error("Prefab path cannot contain ..");
		}
		if (!part.empty() && part != ".") {
			result += "/" + part;
		}
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
	if (!result.ends_with(".prefab.json")) {
		throw std::runtime_error("Prefab must have .prefab.json extension");
	}
	return result;
}

inline bool Component(const Json& component, const char* type) {
	return component.contains(type);
}

inline Json& Transform(Json& object) {
	for (auto& component : object.at("Components")) {
		if (Component(component, "TransformComponentType")) {
			return component;
		}
	}
	throw std::runtime_error("Object has no TransformComponent");
}

inline bool IsPrefabRef(const Json& object) {
	return object.contains("Prefab");
}

std::string ComponentTypeKey(const Json& component);

IdMap ReadMap(const Json& entry);

// Only declared entity-reference fields are visited. Other integer properties
// (particle counts, enum values, resource indices, etc.) are never remapped.
template <class F> void References(Json& object, F&& visit) {
	std::function<void(Json&)> nested = [&](Json& value) {
		if (value.is_object()) {
			if (value.contains("ObjectId") && value.contains("Component") && value.at("Component").is_string()) {
				visit(value["ObjectId"], "ComponentReference.ObjectId");
			}
			for (auto& child : value) {
				nested(child);
			}
		}
		else if (value.is_array()) {
			for (auto& child : value) {
				nested(child);
			}
		}
	};
	for (auto& component : object.at("Components")) {
		nested(component);
		if (Component(component, "RenderTargetComponentType") && component.contains("CameraObjectId")) {
			visit(component["CameraObjectId"], "CameraObjectId");
		}
		if (Component(component, "Joint2DComponentType") && component.contains("PhysicsTargetId")) {
			visit(component["PhysicsTargetId"], "PhysicsTargetId");
		}
	}
}

inline void Remap(Json& object, const IdMap& ids) {
	const auto old = Id(object.at("Id"));
	object["Id"] = ids.at(old);
	const int parent = object.value("ParentId", -1);
	object["ParentId"] = parent > 0 ? ids.at(parent) : -1;
	References(object, [&](Json& value, const char* field) {
		if (value == 0 || value == -1) {
			return;
		}
		const int target = Id(value);
		if (!ids.contains(target)) {
			throw std::runtime_error("Object " + std::to_string(old) + " has external prefab reference: " + field + "=" + std::to_string(target));
		}
		value = ids.at(target);
	});
}

void ValidateObject(const Json& object);

inline void ValidateHierarchy(Json objects, int root = 0, bool requireTransform = true) {
	if (!objects.is_array()) {
		throw std::runtime_error("Objects must be an array");
	}
	std::map<int, std::size_t> indices;
	int roots = 0;
	for (std::size_t i = 0; i < objects.size(); ++i) {
		auto& object = objects[i];
		if (IsPrefabRef(object)) {
			throw std::runtime_error("Nested prefabs are not supported; unpack first");
		}
		ValidateObject(object);
		const auto id = Id(object.at("Id"));
		if (!indices.emplace(id, i).second) {
			throw std::runtime_error("Duplicate entity ID: " + std::to_string(id));
		}
		if (!object.at("Components").is_array()) {
			throw std::runtime_error("Components must be an array");
		}
		if (requireTransform) {
			Transform(object);
			int transforms = 0;
			for (const auto& c : object["Components"]) {
				transforms += Component(c, "TransformComponentType");
			}
			if (transforms != 1) {
				throw std::runtime_error("Object must have exactly one TransformComponent");
			}
		}
		if (object.value("ParentId", -1) <= 0) {
			++roots;
			if (root && id != root) {
				throw std::runtime_error("Prefab has an unexpected root");
			}
		}
	}
	if (root && (roots != 1 || !indices.contains(root))) {
		throw std::runtime_error("Prefab must have one RootId");
	}
	std::vector<unsigned char> colors(objects.size());
	for (std::size_t start = 0; start < objects.size(); ++start) {
		std::vector<std::size_t> chain;
		auto i = start;
		while (colors[i] != 2) {
			if (colors[i] == 1) {
				throw std::runtime_error("Scene parent cycle");
			}
			colors[i] = 1;
			chain.push_back(i);
			const int parent = objects[i].value("ParentId", -1);
			if (parent <= 0) {
				break;
			}
			if (!indices.contains(parent)) {
				throw std::runtime_error("Missing parent: " + std::to_string(parent));
			}
			i = indices.at(parent);
		}
		for (auto item : chain) {
			colors[item] = 2;
		}
		if (requireTransform) {
			References(objects[start], [&](Json& value, const char* field) {
				if (value != 0 && value != -1 && !indices.contains(Id(value))) {
					throw std::runtime_error("Missing entity reference: " + std::string(field));
				}
			});
		}
	}
}

inline void Validate(Json objects, int root = 0) {
	ValidateHierarchy(std::move(objects), root, true);
}

void ValidatePrefab(const Json& asset);
std::vector<std::string> PrefabPaths(const Json& entries);
std::set<int> OwnedIds(const Json& entry);
bool OwnsId(const Json& entry, int objectId);

inline Json WriteMap(const IdMap& ids) {
	Json result = Json::object();
	for (auto [local, runtime] : ids) {
		result[std::to_string(local)] = runtime;
	}
	return result;
}

void MergeLegacyRootOverrides(Json& patch, const Json& legacy);
Json OverridesForLocal(const Json& entry, int localId, int localRoot);
void ApplyObjectOverrides(Json& object, const Json& overrides);

inline Json ComponentTypes(const Json& object) {
	Json types = Json::array();
	for (const auto& component : object.at("Components")) {
		types.push_back(ComponentTypeKey(component));
	}
	return types;
}

inline bool SameStructure(const Json& baseline, const Json& current, bool compareParent = true) {
	if (compareParent && baseline.value("ParentId", -1) != current.value("ParentId", -1)) {
		return false;
	}
	return ComponentTypes(baseline) == ComponentTypes(current);
}

inline Json DiffObject(const Json& baseline, const Json& current) {
	Json patch = Json::object();
	for (const char* key : { "Name", "Tag", "IsActive", "IsVisible" }) {
		const bool baseHas = baseline.contains(key);
		const bool curHas = current.contains(key);
		if (baseHas != curHas || (baseHas && baseline.at(key) != current.at(key))) {
			if (curHas) {
				patch[key] = current.at(key);
			}
		}
	}
	const auto baseTransform = Transform(const_cast<Json&>(baseline));
	const auto curTransform = Transform(const_cast<Json&>(current));
	Json transformPatch = Json::object();
	for (const char* key : { "LocalPosition", "LocalRotation", "LocalScale" }) {
		const bool baseHas = baseTransform.contains(key);
		const bool curHas = curTransform.contains(key);
		if (baseHas != curHas || (baseHas && baseTransform.at(key) != curTransform.at(key))) {
			if (curHas) {
				transformPatch[key] = curTransform.at(key);
			}
		}
	}
	if (!transformPatch.empty()) {
		patch["Transform"] = std::move(transformPatch);
	}
	Json componentPatch = Json::object();
	std::map<std::string, Json> baseByType;
	for (const auto& component : baseline.at("Components")) {
		baseByType.emplace(ComponentTypeKey(component), component);
	}
	for (const auto& component : current.at("Components")) {
		const auto type = ComponentTypeKey(component);
		const auto& baseComponent = baseByType.at(type);
		Json fields = Json::object();
		for (auto it = component.begin(); it != component.end(); ++it) {
			if (it.key() == type) {
				continue;
			}
			const bool baseHas = baseComponent.contains(it.key());
			if (!baseHas || baseComponent.at(it.key()) != it.value()) {
				fields[it.key()] = it.value();
			}
		}
		if (!fields.empty()) {
			componentPatch[type] = std::move(fields);
		}
	}
	if (!componentPatch.empty()) {
		patch["Components"] = std::move(componentPatch);
	}
	return patch;
}

struct Expanded {
	Json objects = Json::array();
	std::vector<Instance> instances;
};

// Reserve persisted IDs for the whole document before allocating new members.
// Overrides use canonical, positive prefab-local IDs. A saved IdMap entry identifies
// a removed member whose edits can be discarded when the instance is compacted.
class Resolver {
public:
	explicit Resolver(const Json& entries);
	Expanded Expand(Json entry, const ReadFn& read);

private:
	int allocate();
	Expanded expandPrefab(Json entry, const ReadFn& read, const std::vector<Json>& defaults, const std::vector<Json>& edits,
		std::map<int, std::vector<Json>>& deferredOverrides);

	std::set<int> mReserved;
	int mNext = 1;
	std::map<std::string, Json> mAssets;
	std::vector<std::string> mStack;
};

Expanded Expand(const Json& document, const ReadFn& read);
Json MakePrefab(Json objects, int root, IdMap& localToScene);
Json Compact(const Json& objects, const std::vector<Instance>& instances);

// Backward-compatible alias used by legacy tests.
inline void Overrides(Json& object, const Json& overrides) {
	ApplyObjectOverrides(object, overrides);
}

} // namespace IKIGAI::SCENE_SYSTEM::DOCUMENT
