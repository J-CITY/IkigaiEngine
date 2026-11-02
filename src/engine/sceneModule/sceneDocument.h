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

inline std::string ComponentTypeKey(const Json& component) {
	static const char* const kKnown[] = { "TransformComponentType", "RenderTargetComponentType", "Joint2DComponentType" };
	for (const char* key : kKnown) {
		if (component.contains(key)) {
			return key;
		}
	}
	for (auto it = component.begin(); it != component.end(); ++it) {
		if (it.key().ends_with("ComponentType")) {
			return it.key();
		}
	}
	throw std::runtime_error("Component has no type tag");
}

inline IdMap ReadMap(const Json& entry) {
	IdMap ids;
	if (!entry.contains("IdMap")) {
		return ids;
	}
	if (!entry["IdMap"].is_object()) {
		throw std::runtime_error("IdMap must be an object");
	}
	for (auto it = entry["IdMap"].begin(); it != entry["IdMap"].end(); ++it) {
		std::size_t used = 0;
		const int local = std::stoi(it.key(), &used);
		if (used != it.key().size() || local <= 0 || std::to_string(local) != it.key()) {
			throw std::runtime_error("Invalid local ID in IdMap");
		}
		ids.emplace(local, Id(it.value()));
	}
	return ids;
}

inline void ReserveEntryIds(const Json& entry, std::set<int>& reserved) {
	const int root = Id(entry.at("Id"));
	if (!reserved.insert(root).second) {
		throw std::runtime_error("Duplicate scene ID");
	}
	if (entry.contains("Prefab")) {
		if (entry.contains("Components")) {
			throw std::runtime_error("Prefab entry cannot contain Components");
		}
		bool rootMapped = false;
		for (auto [local, runtime] : ReadMap(entry)) {
			if (runtime == root) {
				if (rootMapped) {
					throw std::runtime_error("Duplicate root mapping");
				}
				rootMapped = true;
			}
			else if (!reserved.insert(runtime).second) {
				throw std::runtime_error("Colliding prefab ID map");
			}
		}
		if (entry.contains("NestedInstances") && entry["NestedInstances"].is_object()) {
			for (auto& nested : entry["NestedInstances"]) {
				ReserveEntryIds(nested, reserved);
			}
		}
	}
}

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

inline void ValidateAssetObject(const Json& object, int version) {
	if (IsPrefabRef(object)) {
		if (version < 2) {
			throw std::runtime_error("Nested prefabs require PrefabVersion 2");
		}
		if (object.contains("Components")) {
			throw std::runtime_error("Prefab reference cannot contain Components");
		}
		Path(object.at("Prefab").get<std::string>());
		Id(object.at("Id"));
		if (object.contains("IdMap")) {
			ReadMap(object);
		}
		if (object.contains("Overrides") && !object["Overrides"].is_object()) {
			throw std::runtime_error("Overrides must be an object");
		}
		if (object.contains("NestedInstances") && !object["NestedInstances"].is_object()) {
			throw std::runtime_error("NestedInstances must be an object");
		}
		if (object.contains("NestedInstances")) {
			for (auto& nested : object["NestedInstances"]) {
				if (!nested.is_object()) {
					throw std::runtime_error("Nested instance entry must be an object");
				}
				Path(nested.at("Prefab").get<std::string>());
				Id(nested.at("Id"));
				ReadMap(nested);
			}
		}
		return;
	}
	if (!object.at("Components").is_array()) {
		throw std::runtime_error("Components must be an array");
	}
	Transform(const_cast<Json&>(object));
	int transforms = 0;
	for (const auto& c : object["Components"]) {
		transforms += Component(c, "TransformComponentType");
	}
	if (transforms != 1) {
		throw std::runtime_error("Object must have exactly one TransformComponent");
	}
}

inline void ValidateAssetObjects(Json objects, int root, int version) {
	if (!objects.is_array()) {
		throw std::runtime_error("Objects must be an array");
	}
	std::map<int, std::size_t> indices;
	int roots = 0;
	for (std::size_t i = 0; i < objects.size(); ++i) {
		auto& object = objects[i];
		ValidateAssetObject(object, version);
		const auto id = Id(object.at("Id"));
		if (!indices.emplace(id, i).second) {
			throw std::runtime_error("Duplicate entity ID: " + std::to_string(id));
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
		if (!IsPrefabRef(objects[start])) {
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

inline void ValidatePrefab(const Json& asset) {
	const int version = asset.at("PrefabVersion").get<int>();
	if (version != 1 && version != 2) {
		throw std::runtime_error("Unsupported PrefabVersion");
	}
	ValidateAssetObjects(asset.at("Objects"), Id(asset.at("RootId")), version);
}

inline Json WriteMap(const IdMap& ids) {
	Json result = Json::object();
	for (auto [local, runtime] : ids) {
		result[std::to_string(local)] = runtime;
	}
	return result;
}

inline void MergeLegacyRootOverrides(Json& patch, const Json& legacy) {
	if (!legacy.is_object() || legacy.empty()) {
		return;
	}
	for (auto it = legacy.begin(); it != legacy.end(); ++it) {
		if (it.key() == "Transform") {
			if (!patch.contains("Transform")) {
				patch["Transform"] = Json::object();
			}
			for (auto p = it.value().begin(); p != it.value().end(); ++p) {
				patch["Transform"][p.key()] = p.value();
			}
		}
		else if (it.key() == "Name" || it.key() == "Tag" || it.key() == "IsActive" || it.key() == "IsVisible") {
			patch[it.key()] = it.value();
		}
		else {
			throw std::runtime_error("Unsupported root override: " + it.key());
		}
	}
}

inline Json OverridesForLocal(const Json& entry, int localId, int localRoot) {
	Json patch = Json::object();
	if (entry.contains("Overrides") && entry["Overrides"].is_object()) {
		const auto key = std::to_string(localId);
		if (entry["Overrides"].contains(key)) {
			patch = entry["Overrides"][key];
		}
	}
	if (localId == localRoot && entry.contains("RootOverrides")) {
		MergeLegacyRootOverrides(patch, entry["RootOverrides"]);
	}
	return patch;
}

inline void ApplyObjectOverrides(Json& object, const Json& overrides) {
	if (!overrides.is_object() || overrides.empty()) {
		return;
	}
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		if (it.key() == "Transform") {
			if (!it.value().is_object()) {
				throw std::runtime_error("Transform override must be an object");
			}
			for (auto p = it.value().begin(); p != it.value().end(); ++p) {
				if (p.key() != "LocalPosition" && p.key() != "LocalRotation" && p.key() != "LocalScale") {
					throw std::runtime_error("Unsupported Transform override: " + p.key());
				}
				Transform(object)[p.key()] = p.value();
			}
		}
		else if (it.key() == "Components") {
			if (!it.value().is_object()) {
				throw std::runtime_error("Components override must be an object");
			}
			for (auto c = it.value().begin(); c != it.value().end(); ++c) {
				Json* target = nullptr;
				for (auto& component : object.at("Components")) {
					if (Component(component, c.key().c_str())) {
						target = &component;
						break;
					}
				}
				if (!target) {
					throw std::runtime_error("Override targets missing component: " + c.key());
				}
				if (!c.value().is_object()) {
					throw std::runtime_error("Component override must be an object");
				}
				for (auto p = c.value().begin(); p != c.value().end(); ++p) {
					(*target)[p.key()] = p.value();
				}
			}
		}
		else if (it.key() == "Name" || it.key() == "Tag" || it.key() == "IsActive" || it.key() == "IsVisible") {
			object[it.key()] = it.value();
		}
		else {
			throw std::runtime_error("Unsupported object override: " + it.key());
		}
	}
}

inline Json ComponentTypes(const Json& object) {
	Json types = Json::array();
	for (const auto& component : object.at("Components")) {
		types.push_back(ComponentTypeKey(component));
	}
	return types;
}

inline bool SameStructure(const Json& baseline, const Json& current) {
	if (baseline.value("ParentId", -1) != current.value("ParentId", -1)) {
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
			if (it.key().ends_with("ComponentType")) {
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

inline Json LoadPrefabAsset(const std::string& path, const ReadFn& read, std::map<std::string, Json>& cache) {
	const auto normalized = Path(path);
	if (!cache.contains(normalized)) {
		cache.emplace(normalized, read(normalized));
	}
	return cache.at(normalized);
}

inline void PushExpandPath(const std::string& path, std::vector<std::string>& stack) {
	const auto normalized = Path(path);
	if (std::find(stack.begin(), stack.end(), normalized) != stack.end()) {
		throw std::runtime_error("Prefab expand cycle: " + normalized);
	}
	stack.push_back(normalized);
}

struct Expanded {
	Json objects = Json::array();
	std::vector<Instance> instances;
};

// One resolver per document. All saved IDs are reserved before allocating any
// newly added prefab nodes, including IDs belonging to later array entries.
class Resolver {
	std::set<int> reserved;
	int next = 1;

	int Allocate() {
		while (reserved.contains(next)) {
			if (++next >= std::numeric_limits<int>::max()) {
				throw std::runtime_error("No free scene IDs");
			}
		}
		reserved.insert(next);
		return next;
	}

	IdMap BuildLiveMap(const Json& asset, const Json& entry, int localRoot, int runtimeRoot) {
		auto ids = ReadMap(entry);
		if (ids.contains(localRoot) && ids.at(localRoot) != runtimeRoot) {
			throw std::runtime_error("Root ID mapping mismatch");
		}
		for (auto [local, runtime] : ids) {
			if (runtime == runtimeRoot && local != localRoot) {
				throw std::runtime_error("Wrong local root mapping");
			}
		}
		ids[localRoot] = runtimeRoot;
		std::set<int> plannedLocals;
		for (const auto& object : asset.at("Objects")) {
			plannedLocals.insert(Id(object.at("Id")));
			if (IsPrefabRef(object)) {
				for (auto [nestedLocal, parentLocal] : ReadMap(object)) {
					plannedLocals.insert(parentLocal);
					(void)nestedLocal;
				}
			}
		}
		IdMap live;
		for (const int planned : plannedLocals) {
			live[planned] = ids.contains(planned) ? ids.at(planned) : Allocate();
		}
		return live;
	}

	IdMap WriteInstanceMap(const Json& asset, const IdMap& live, const Json& nestedInstances) {
		std::set<int> locals;
		for (const auto& object : asset.at("Objects")) {
			locals.insert(Id(object.at("Id")));
			if (IsPrefabRef(object)) {
				for (auto [nestedLocal, parentLocal] : ReadMap(object)) {
					locals.insert(parentLocal);
					(void)nestedLocal;
				}
			}
		}
		IdMap written;
		for (const int local : locals) {
			written[local] = live.at(local);
		}
		(void)nestedInstances;
		return written;
	}

	void ExpandNestedEntry(Json nestedEntry, int nestedLocalRoot, int nestedRuntimeRoot, const IdMap& parentLive, const ReadFn& read,
		std::map<std::string, Json>& cache, std::vector<std::string>& stack, Expanded& result, Json& nestedInstancesOut, Json& parentBaseline) {
		const int parentLocal = nestedEntry.value("ParentId", -1);
		nestedEntry["Id"] = nestedRuntimeRoot;
		nestedEntry["ParentId"] = parentLocal > 0 ? parentLive.at(parentLocal) : nestedEntry.value("ParentId", -1);
		IdMap nestedRuntimeMap;
		for (auto [nestedLocal, parentLocalId] : ReadMap(nestedEntry)) {
			nestedRuntimeMap[nestedLocal] = parentLive.at(parentLocalId);
		}
		nestedEntry["IdMap"] = WriteMap(nestedRuntimeMap);
		const auto nestedPath = nestedEntry.at("Prefab").get<std::string>();
		PushExpandPath(nestedPath, stack);
		try {
			const auto nestedAsset = LoadPrefabAsset(nestedPath, read, cache);
			ValidatePrefab(nestedAsset);
			auto nestedExpanded = ExpandPrefabInstance(std::move(nestedEntry), nestedAsset, read, cache, stack);
			for (auto& object : nestedExpanded.objects) {
				result.objects.push_back(std::move(object));
			}
			if (!nestedExpanded.instances.empty()) {
				nestedInstancesOut[std::to_string(nestedLocalRoot)] = std::move(nestedExpanded.instances.front().entry);
				for (const auto& object : nestedExpanded.instances.front().baseline) {
					parentBaseline.push_back(object);
				}
			}
		} catch (...) {
			stack.pop_back();
			throw;
		}
		stack.pop_back();
	}

	Expanded ExpandPrefabInstance(
		Json entry, const Json& asset, const ReadFn& read, std::map<std::string, Json>& cache, std::vector<std::string>& stack) {
		Expanded result;
		const auto path = Path(entry.at("Prefab").get<std::string>());
		const int localRoot = Id(asset.at("RootId"));
		const int runtimeRoot = Id(entry.at("Id"));
		const auto live = BuildLiveMap(asset, entry, localRoot, runtimeRoot);
		Json nestedInstances = entry.contains("NestedInstances") ? entry["NestedInstances"] : Json::object();
		Json baseline = Json::array();
		for (auto object : asset.at("Objects")) {
			const auto localId = Id(object.at("Id"));
			if (IsPrefabRef(object)) {
				Json nestedEntry = object;
				if (nestedInstances.contains(std::to_string(localId))) {
					for (auto it = nestedInstances[std::to_string(localId)].begin(); it != nestedInstances[std::to_string(localId)].end(); ++it) {
						nestedEntry[it.key()] = it.value();
					}
				}
				ExpandNestedEntry(std::move(nestedEntry), localId, live.at(localId), live, read, cache, stack, result, nestedInstances, baseline);
				continue;
			}
			const bool isRoot = localId == localRoot;
			Remap(object, live);
			if (isRoot) {
				object["ParentId"] = entry.value("ParentId", -1);
			}
			baseline.push_back(object);
			ApplyObjectOverrides(object, OverridesForLocal(entry, localId, localRoot));
			result.objects.push_back(std::move(object));
		}
		entry["Prefab"] = path;
		entry["IdMap"] = WriteMap(WriteInstanceMap(asset, live, nestedInstances));
		if (!nestedInstances.empty()) {
			entry["NestedInstances"] = std::move(nestedInstances);
		}
		else {
			entry.erase("NestedInstances");
		}
		result.instances.push_back({ std::move(entry), std::move(baseline) });
		return result;
	}

public:
	explicit Resolver(const Json& entries) {
		if (!entries.is_array()) {
			throw std::runtime_error("Scene Objects must be an array");
		}
		for (const auto& entry : entries) {
			ReserveEntryIds(entry, reserved);
		}
		for (auto entry : entries) {
			const int parent = entry.value("ParentId", -1);
			if (parent > 0) {
				reserved.insert(parent);
			}
			if (!entry.contains("Prefab")) {
				References(entry, [&](Json& value, const char*) {
					if (value != 0 && value != -1) {
						reserved.insert(Id(value));
					}
				});
			}
		}
	}

	Expanded Expand(Json entry, const Json& asset, const ReadFn& read, std::map<std::string, Json>& cache, std::vector<std::string>& stack) {
		if (!entry.contains("Prefab")) {
			Expanded result;
			result.objects.push_back(std::move(entry));
			return result;
		}
		const auto path = Path(entry.at("Prefab").get<std::string>());
		try {
			PushExpandPath(path, stack);
			const auto resolvedAsset = asset.is_null() || asset.empty() ? LoadPrefabAsset(path, read, cache) : asset;
			ValidatePrefab(resolvedAsset);
			auto expanded = ExpandPrefabInstance(std::move(entry), resolvedAsset, read, cache, stack);
			stack.pop_back();
			return expanded;
		} catch (const std::exception& e) {
			if (!stack.empty() && stack.back() == path) {
				stack.pop_back();
			}
			throw std::runtime_error(path + ": " + e.what());
		}
	}
};

inline Expanded Expand(const Json& document, const ReadFn& read) {
	Resolver resolver(document.at("Objects"));
	Expanded result;
	std::map<std::string, Json> cache;
	std::vector<std::string> stack;
	for (auto entry : document.at("Objects")) {
		Json asset;
		if (entry.contains("Prefab")) {
			const auto path = Path(entry.at("Prefab").get<std::string>());
			asset = LoadPrefabAsset(path, read, cache);
		}
		auto expanded = resolver.Expand(std::move(entry), asset, read, cache, stack);
		for (auto& object : expanded.objects) {
			result.objects.push_back(std::move(object));
		}
		for (auto& instance : expanded.instances) {
			result.instances.push_back(std::move(instance));
		}
	}
	Validate(result.objects);
	return result;
}

inline Json MakePrefab(Json objects, int root, IdMap& localToScene) {
	for (auto& object : objects) {
		if (Id(object.at("Id")) == root) {
			object["ParentId"] = -1;
		}
	}
	int version = 1;
	for (const auto& object : objects) {
		if (IsPrefabRef(object)) {
			version = 2;
			break;
		}
	}
	ValidateAssetObjects(objects, root, version);
	IdMap sceneToLocal;
	int local = 1;
	for (auto& object : objects) {
		const auto id = Id(object.at("Id"));
		sceneToLocal[id] = local;
		localToScene[local++] = id;
	}
	int nextLocal = local - 1;
	for (auto& object : objects) {
		if (IsPrefabRef(object)) {
			if (object.contains("IdMap")) {
				IdMap remapped;
				for (auto [nestedLocal, sceneId] : ReadMap(object)) {
					if (sceneToLocal.contains(sceneId)) {
						remapped[nestedLocal] = sceneToLocal.at(sceneId);
					}
					else {
						remapped[nestedLocal] = ++nextLocal;
					}
				}
				object["IdMap"] = WriteMap(remapped);
			}
			const auto old = Id(object.at("Id"));
			object["Id"] = sceneToLocal.at(old);
			const int parent = object.value("ParentId", -1);
			object["ParentId"] = parent > 0 ? sceneToLocal.at(parent) : -1;
			continue;
		}
		Remap(object, sceneToLocal);
	}
	return Json { { "PrefabVersion", version }, { "RootId", sceneToLocal.at(root) }, { "Objects", objects } };
}

inline Json RootValues(Json object) {
	Json result;
	for (const char* key : { "Name", "Tag", "IsActive", "IsVisible" }) {
		if (object.contains(key)) {
			result[key] = object[key];
		}
	}
	auto transform = Transform(object);
	transform.erase("TransformComponentType");
	result["Transform"] = std::move(transform);
	return result;
}

inline Json BuildOverrides(const Instance& instance, const Json& objects) {
	std::map<int, Json> byId;
	for (const auto& object : objects) {
		byId.emplace(Id(object.at("Id")), object);
	}
	std::map<int, Json> baselineById;
	for (const auto& object : instance.baseline) {
		baselineById.emplace(Id(object.at("Id")), object);
	}
	Json overrides = Json::object();
	for (auto [local, runtime] : ReadMap(instance.entry)) {
		if (!byId.contains(runtime) || !baselineById.contains(runtime)) {
			throw std::runtime_error("Missing prefab object during save");
		}
		const auto& baseline = baselineById.at(runtime);
		const auto& current = byId.at(runtime);
		if (!SameStructure(baseline, current)) {
			throw std::runtime_error(instance.entry.at("Prefab").get<std::string>() + ": unsupported instance edit; unpack prefab before saving");
		}
		const auto patch = DiffObject(baseline, current);
		if (!patch.empty()) {
			overrides[std::to_string(local)] = patch;
		}
	}
	return overrides;
}

inline void CompactNestedEntry(Json& nestedEntry, const Json& parentBaseline, const Json& objects) {
	Json nestedBaseline = Json::array();
	for (auto [local, runtime] : ReadMap(nestedEntry)) {
		for (const auto& baseObject : parentBaseline) {
			if (Id(baseObject.at("Id")) == runtime) {
				nestedBaseline.push_back(baseObject);
			}
		}
		(void)local;
	}
	Instance nestedInstance { nestedEntry, nestedBaseline };
	nestedEntry["Overrides"] = BuildOverrides(nestedInstance, objects);
	nestedEntry.erase("RootOverrides");
	if (nestedEntry.contains("NestedInstances")) {
		for (auto it = nestedEntry["NestedInstances"].begin(); it != nestedEntry["NestedInstances"].end(); ++it) {
			CompactNestedEntry(it.value(), nestedBaseline, objects);
		}
	}
}

inline Json Compact(const Json& objects, const std::vector<Instance>& instances) {
	Validate(objects);
	std::map<int, Json> byId;
	for (const auto& object : objects) {
		byId.emplace(Id(object.at("Id")), object);
	}
	std::map<int, Json> entries;
	std::set<int> members;
	for (const auto& instance : instances) {
		auto entry = instance.entry;
		const int root = Id(entry.at("Id"));
		if (instance.baseline.empty()) {
			throw std::runtime_error("Missing prefab baseline");
		}
		std::set<int> owned;
		for (auto [local, id] : ReadMap(entry)) {
			if (!members.insert(id).second) {
				throw std::runtime_error("Overlapping prefab instances");
			}
			owned.insert(id);
		}
		for (const auto& object : objects) {
			if (!owned.contains(Id(object.at("Id"))) && owned.contains(object.value("ParentId", -1))) {
				throw std::runtime_error("Unpack prefab before adding children to an instance");
			}
		}
		entry["ParentId"] = byId.at(root).value("ParentId", -1);
		entry["Overrides"] = BuildOverrides(instance, objects);
		entry.erase("RootOverrides");
		if (entry.contains("NestedInstances")) {
			for (auto it = entry["NestedInstances"].begin(); it != entry["NestedInstances"].end(); ++it) {
				CompactNestedEntry(it.value(), instance.baseline, objects);
			}
		}
		entries.emplace(root, std::move(entry));
	}
	Json result = Json::array();
	for (const auto& object : objects) {
		const int id = Id(object.at("Id"));
		if (entries.contains(id)) {
			result.push_back(entries.at(id));
		}
		else if (!members.contains(id)) {
			result.push_back(object);
		}
	}
	return Json { { "Objects", result } };
}

// Backward-compatible alias used by legacy tests.
inline void Overrides(Json& object, const Json& overrides) {
	ApplyObjectOverrides(object, overrides);
}

} // namespace IKIGAI::SCENE_SYSTEM::DOCUMENT
