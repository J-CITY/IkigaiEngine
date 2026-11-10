#include "sceneDocument.h"
#include "utilsModule/serialization/jsonValueAdaptor.h"
#include <serdepp/serde.hpp>

namespace IKIGAI::SCENE_SYSTEM::DOCUMENT {
namespace {
	constexpr std::size_t MaxPrefabDepth = 64;

	struct PrefabDescriptor {
		int PrefabVersion = 1;
		int RootId = 0;
		std::vector<Json> Objects;

		template <class Context> constexpr static void serde(Context& context, PrefabDescriptor& value) {
			serde::serde_struct(context, value)
				.field(&PrefabDescriptor::PrefabVersion, "PrefabVersion")
				.field(&PrefabDescriptor::RootId, "RootId")
				.field(&PrefabDescriptor::Objects, "Objects");
		}
	};

	struct ReferenceDescriptor {
		std::string Prefab;
		int Id = 0;
		int ParentId = -1;

		template <class Context> constexpr static void serde(Context& context, ReferenceDescriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&ReferenceDescriptor::Prefab, "Prefab")
				.field(&ReferenceDescriptor::Id, "Id")
				.field(&ReferenceDescriptor::ParentId, "ParentId", default_ { -1 });
		}
	};

	ReferenceDescriptor ReadReference(const Json& entry) {
		if (entry.contains("Components")) {
			throw std::runtime_error("Prefab reference cannot contain Components");
		}
		if (!entry.at("Prefab").is_string() || (entry.contains("ParentId") && !entry.at("ParentId").is_number_integer())) {
			throw std::runtime_error("Invalid prefab reference");
		}
		Json identity { { "Prefab", entry.at("Prefab") }, { "Id", Id(entry.at("Id")) }, { "ParentId", entry.value("ParentId", -1) } };
		auto result = serde::deserialize<ReferenceDescriptor>(identity);
		result.Prefab = Path(std::move(result.Prefab));
		return result;
	}

	const Json& NestedInstances(const Json& entry) {
		static const Json empty = Json::object();
		const auto nested = entry.find("NestedInstances");
		if (nested == entry.end()) {
			return empty;
		}
		if (!nested->is_object()) {
			throw std::runtime_error("NestedInstances must be an object");
		}
		return *nested;
	}

	void ValidatePatchFields(const Json& entry) {
		for (const char* key : { "Overrides", "RootOverrides" }) {
			if (entry.contains(key) && !entry.at(key).is_object()) {
				throw std::runtime_error(std::string(key) + " must be an object");
			}
		}
	}

	std::set<int> CollectOwnedIds(const Json& entry, std::size_t depth) {
		if (depth >= MaxPrefabDepth) {
			throw std::runtime_error("Prefab metadata exceeds the depth limit");
		}
		const int root = Id(entry.at("Id"));
		std::set<int> owned { root };
		if (!IsPrefabRef(entry)) {
			return owned;
		}
		ReadReference(entry);
		ValidatePatchFields(entry);
		std::set<int> mapped;
		for (const auto& [local, id] : ReadMap(entry)) {
			if (!mapped.insert(id).second) {
				throw std::runtime_error("Colliding prefab ID map");
			}
			owned.insert(id);
		}
		std::set<int> nestedIds;
		for (const auto& child : NestedInstances(entry)) {
			if (!IsPrefabRef(child)) {
				throw std::runtime_error("Invalid nested prefab instance");
			}
			for (const int id : CollectOwnedIds(child, depth + 1)) {
				if (!nestedIds.insert(id).second) {
					throw std::runtime_error("Colliding sibling prefab ID map");
				}
			}
		}
		// A nested map may alias an object already named in its ancestor's map.
		// Siblings and separate top-level entries must own disjoint sets.
		owned.merge(nestedIds);
		return owned;
	}

	PrefabDescriptor ReadPrefab(Json asset) {
		Id(asset.at("RootId"));
		const auto& version = asset.at("PrefabVersion");
		if (!version.is_number_integer() || (version != 1 && version != 2)) {
			throw std::runtime_error("Unsupported PrefabVersion");
		}
		if (!asset.at("Objects").is_array()) {
			throw std::runtime_error("Objects must be an array");
		}
		return serde::deserialize<PrefabDescriptor>(asset);
	}

	void ValidateAssetObjects(const std::vector<Json>& objects, int root, int version) {
		std::map<int, int> parents;
		std::map<int, int> owners;
		int roots = 0;
		for (const auto& object : objects) {
			const int id = Id(object.at("Id"));
			const int parent = object.value("ParentId", -1);
			parents.emplace(id, parent);
			if (parent <= 0) {
				++roots;
				if (id != root) {
					throw std::runtime_error("Prefab has an unexpected root");
				}
			}
			if (IsPrefabRef(object)) {
				if (version < 2) {
					throw std::runtime_error("Nested prefabs require PrefabVersion 2");
				}
			}
			else {
				const auto& components = object.at("Components");
				if (!components.is_array() || std::count_if(components.begin(), components.end(), [](const Json& component) {
						return Component(component, "TransformComponentType");
					}) != 1) {
					throw std::runtime_error("Object must have exactly one TransformComponent");
				}
			}
			for (const int owned : CollectOwnedIds(object, 0)) {
				if (!owners.emplace(owned, id).second) {
					throw std::runtime_error("Colliding prefab asset ID: " + std::to_string(owned));
				}
			}
		}
		if (roots != 1 || !parents.contains(root)) {
			throw std::runtime_error("Prefab must have one RootId");
		}
		std::set<int> validated;
		for (const auto& [id, parent] : parents) {
			int current = id;
			std::set<int> chain;
			while (current > 0 && !validated.contains(current)) {
				if (!chain.insert(current).second) {
					throw std::runtime_error("Prefab parent cycle");
				}
				current = parents.at(current);
				if (current > 0) {
					if (!owners.contains(current)) {
						throw std::runtime_error("Missing prefab parent: " + std::to_string(current));
					}
					current = owners.at(current);
				}
			}
			validated.merge(chain);
		}
		for (auto object : objects) {
			if (!IsPrefabRef(object)) {
				References(object, [&](Json& value, const char* field) {
					if (value != 0 && value != -1 && !owners.contains(Id(value))) {
						throw std::runtime_error("Missing prefab entity reference: " + std::string(field));
					}
				});
			}
		}
	}

	int MapId(int id, const IdMap& ids) {
		const auto found = ids.find(id);
		if (found == ids.end()) {
			throw std::runtime_error("Prefab contains an external entity reference: " + std::to_string(id));
		}
		return found->second;
	}

	template <class Visitor> void VisitOverrideReferences(Json& patch, const Visitor& visit) {
		auto components = patch.find("Components");
		if (components == patch.end() || !components->is_object()) {
			return;
		}
		Json wrapper { { "Components", Json::array() } };
		for (auto& [type, component] : components->items()) {
			if (!component.is_object()) {
				continue;
			}
			auto fields = component;
			fields[type] = true;
			wrapper["Components"].push_back(std::move(fields));
			References(wrapper, visit);
			auto& remapped = wrapper["Components"].front();
			remapped.erase(type);
			component = std::move(remapped);
			wrapper["Components"].clear();
		}
	}

	void RemapOverride(Json& patch, const IdMap& ids) {
		VisitOverrideReferences(patch, [&](Json& value, const char*) {
			if (value != 0 && value != -1) {
				value = MapId(Id(value), ids);
			}
		});
	}

	void ReserveOverrideTargets(const Json& entry, std::set<int>& reserved) {
		const auto reserve = [&](Json& value, const char*) {
			if (value != 0 && value != -1) {
				reserved.insert(Id(value));
			}
		};
		if (const auto overrides = entry.find("Overrides"); overrides != entry.end()) {
			for (auto patch : *overrides) {
				VisitOverrideReferences(patch, reserve);
			}
		}
		if (const auto legacy = entry.find("RootOverrides"); legacy != entry.end()) {
			auto patch = *legacy;
			VisitOverrideReferences(patch, reserve);
		}
		for (const auto& child : NestedInstances(entry)) {
			ReserveOverrideTargets(child, reserved);
		}
	}

	void RemapPrefabEntry(Json& entry, const IdMap& ids) {
		entry["Id"] = MapId(Id(entry.at("Id")), ids);
		const int parent = entry.value("ParentId", -1);
		entry["ParentId"] = parent > 0 ? MapId(parent, ids) : -1;
		auto map = ReadMap(entry);
		for (auto& [local, target] : map) {
			target = MapId(target, ids);
		}
		entry["IdMap"] = WriteMap(map);
		if (entry.contains("Overrides")) {
			for (auto& patch : entry.at("Overrides")) {
				RemapOverride(patch, ids);
			}
		}
		if (entry.contains("RootOverrides")) {
			RemapOverride(entry.at("RootOverrides"), ids);
		}
		if (entry.contains("NestedInstances")) {
			for (auto& child : entry.at("NestedInstances")) {
				RemapPrefabEntry(child, ids);
			}
		}
	}

	void AppendLayers(const std::vector<Json>& parentLayers, const std::string& key, const std::string& path, std::vector<Json>& childLayers) {
		for (const auto& parent : parentLayers) {
			const auto& children = NestedInstances(parent);
			const auto child = children.find(key);
			if (child != children.end() && ReadReference(*child).Prefab == path) {
				childLayers.push_back(*child);
			}
		}
	}

	void MergeMappings(IdMap& ids, const std::vector<Json>& layers, int root) {
		for (const auto& layer : layers) {
			if (Id(layer.at("Id")) != root) {
				throw std::runtime_error("Nested prefab root ID mapping mismatch");
			}
			for (const auto& [local, target] : ReadMap(layer)) {
				const auto [mapped, inserted] = ids.emplace(local, target);
				if (!inserted && mapped->second != target) {
					throw std::runtime_error("Nested prefab ID alias mismatch");
				}
			}
		}
	}

	using ObjectIndex = std::map<int, const Json*>;

	ObjectIndex IndexObjects(const Json& objects) {
		ObjectIndex result;
		for (const auto& object : objects) {
			result.emplace(Id(object.at("Id")), &object);
		}
		return result;
	}

	void CompactOverrides(Json& entry, const ObjectIndex& baseline, const ObjectIndex& current, std::set<int>& processed, int outerRoot) {
		if (entry.contains("NestedInstances")) {
			for (auto& child : entry.at("NestedInstances")) {
				CompactOverrides(child, baseline, current, processed, outerRoot);
			}
		}
		Json overrides = Json::object();
		for (const auto& [local, runtime] : ReadMap(entry)) {
			if (!processed.insert(runtime).second) {
				continue;
			}
			if (!baseline.contains(runtime) || !current.contains(runtime)) {
				throw std::runtime_error("Missing prefab object during save");
			}
			const auto& base = *baseline.at(runtime);
			const auto& object = *current.at(runtime);
			if (!SameStructure(base, object, runtime != outerRoot)) {
				throw std::runtime_error(entry.at("Prefab").get<std::string>() + ": unsupported instance edit; unpack prefab before saving");
			}
			auto patch = DiffObject(base, object);
			if (!patch.empty()) {
				overrides[std::to_string(local)] = std::move(patch);
			}
		}
		entry["Overrides"] = std::move(overrides);
		entry.erase("RootOverrides");
	}
}

std::set<int> OwnedIds(const Json& entry) {
	return CollectOwnedIds(entry, 0);
}

bool OwnsId(const Json& entry, int objectId) {
	if (entry.at("Id") == objectId) {
		return true;
	}
	if (const auto map = entry.find("IdMap"); map != entry.end() && std::find(map->begin(), map->end(), objectId) != map->end()) {
		return true;
	}
	const auto& children = NestedInstances(entry);
	return std::any_of(children.begin(), children.end(), [objectId](const Json& child) { return OwnsId(child, objectId); });
}

std::vector<std::string> PrefabPaths(const Json& entries) {
	if (!entries.is_array()) {
		throw std::runtime_error("Scene Objects must be an array");
	}
	std::vector<std::string> paths;
	for (const auto& entry : entries) {
		if (IsPrefabRef(entry)) {
			paths.push_back(ReadReference(entry).Prefab);
		}
	}
	return paths;
}

void ValidatePrefab(const Json& asset) {
	const auto descriptor = ReadPrefab(asset);
	ValidateAssetObjects(descriptor.Objects, descriptor.RootId, descriptor.PrefabVersion);
}

Resolver::Resolver(const Json& entries) {
	if (!entries.is_array()) {
		throw std::runtime_error("Scene Objects must be an array");
	}
	for (const auto& entry : entries) {
		for (const int id : OwnedIds(entry)) {
			if (!mReserved.insert(id).second) {
				throw std::runtime_error("Duplicate scene ID: " + std::to_string(id));
			}
		}
	}
	for (auto entry : entries) {
		const int parent = entry.value("ParentId", -1);
		if (parent > 0) {
			mReserved.insert(parent);
		}
		if (IsPrefabRef(entry)) {
			ReserveOverrideTargets(entry, mReserved);
		}
		else {
			References(entry, [&](Json& value, const char*) {
				if (value != 0 && value != -1) {
					mReserved.insert(Id(value));
				}
			});
		}
	}
}

int Resolver::allocate() {
	while (mReserved.contains(mNext)) {
		if (++mNext >= std::numeric_limits<int>::max()) {
			throw std::runtime_error("No free scene IDs");
		}
	}
	mReserved.insert(mNext);
	return mNext;
}

Expanded Resolver::expandPrefab(Json entry, const ReadFn& read, const std::vector<Json>& defaults, const std::vector<Json>& edits,
	std::map<int, std::vector<Json>>& deferredOverrides) {
	const auto reference = ReadReference(entry);
	const auto& path = reference.Prefab;
	if (std::find(mStack.begin(), mStack.end(), path) != mStack.end()) {
		std::string chain;
		for (const auto& ancestor : mStack) {
			chain += ancestor + " -> ";
		}
		throw std::runtime_error("Prefab expand cycle: " + chain + path);
	}
	if (mStack.size() == MaxPrefabDepth) {
		throw std::runtime_error("Prefab expand depth limit: " + path);
	}
	mStack.push_back(path);
	struct PopPath {
		std::vector<std::string>& stack;
		~PopPath() {
			stack.pop_back();
		}
	} popPath { mStack };
	auto cached = mAssets.find(path);
	if (cached == mAssets.end()) {
		auto asset = read(path);
		ValidatePrefab(asset);
		cached = mAssets.emplace(path, std::move(asset)).first;
	}
	const auto& asset = cached->second;
	const int localRoot = Id(asset.at("RootId"));
	IdMap persisted { { localRoot, reference.Id } };
	MergeMappings(persisted, defaults, reference.Id);
	MergeMappings(persisted, edits, reference.Id);
	std::set<int> planned;
	for (const auto& object : asset.at("Objects")) {
		planned.merge(OwnedIds(object));
	}
	IdMap live;
	std::set<int> liveIds;
	for (const int local : planned) {
		const int runtime = persisted.contains(local) ? persisted.at(local) : allocate();
		if (!liveIds.insert(runtime).second) {
			throw std::runtime_error("Colliding live prefab ID map: " + path);
		}
		live[local] = runtime;
	}
	Json nested = Json::object();
	Expanded result;
	for (auto object : asset.at("Objects")) {
		const int local = Id(object.at("Id"));
		if (!IsPrefabRef(object)) {
			Remap(object, live);
			if (local == localRoot) {
				object["ParentId"] = reference.ParentId;
			}
			result.objects.push_back(std::move(object));
			continue;
		}
		RemapPrefabEntry(object, live);
		if (local == localRoot) {
			object["ParentId"] = reference.ParentId;
		}
		const auto key = std::to_string(local);
		const auto childPath = ReadReference(object).Prefab;
		std::vector<Json> childDefaults { object };
		std::vector<Json> childEdits;
		AppendLayers(defaults, key, childPath, childDefaults);
		AppendLayers(edits, key, childPath, childEdits);
		auto child = expandPrefab(std::move(object), read, childDefaults, childEdits, deferredOverrides);
		nested[key] = std::move(child.instances.front().entry);
		for (auto& value : child.objects) {
			result.objects.push_back(std::move(value));
		}
	}
	std::map<int, int> locals;
	for (const auto& [local, runtime] : live) {
		locals.emplace(runtime, local);
	}
	for (auto& object : result.objects) {
		const int runtime = Id(object.at("Id"));
		const auto local = locals.find(runtime);
		if (local == locals.end()) {
			continue;
		}
		for (const auto& layer : defaults) {
			ApplyObjectOverrides(object, OverridesForLocal(layer, local->second, localRoot));
		}
		for (const auto& layer : edits) {
			auto patch = OverridesForLocal(layer, local->second, localRoot);
			if (!patch.empty()) {
				deferredOverrides[runtime].push_back(std::move(patch));
			}
		}
	}
	Json normalized { { "Prefab", path }, { "Id", reference.Id }, { "ParentId", reference.ParentId }, { "IdMap", WriteMap(live) } };
	if (!nested.empty()) {
		normalized["NestedInstances"] = std::move(nested);
	}
	result.instances.push_back({ std::move(normalized), Json::array() });
	return result;
}

Expanded Resolver::Expand(Json entry, const ReadFn& read) {
	if (!IsPrefabRef(entry)) {
		Expanded result;
		result.objects.push_back(std::move(entry));
		return result;
	}
	const auto path = ReadReference(entry).Prefab;
	try {
		std::map<int, std::vector<Json>> deferredOverrides;
		auto expanded = expandPrefab(entry, read, {}, { entry }, deferredOverrides);
		expanded.instances.front().baseline = expanded.objects;
		for (auto& object : expanded.objects) {
			const auto found = deferredOverrides.find(Id(object.at("Id")));
			if (found != deferredOverrides.end()) {
				for (const auto& patch : found->second) {
					ApplyObjectOverrides(object, patch);
				}
			}
		}
		return expanded;
	} catch (const std::exception& error) {
		throw std::runtime_error(path + ": " + error.what());
	}
}

Expanded Expand(const Json& document, const ReadFn& read) {
	Resolver resolver(document.at("Objects"));
	Expanded result;
	for (auto entry : document.at("Objects")) {
		auto expanded = resolver.Expand(std::move(entry), read);
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

Json MakePrefab(Json objects, int root, IdMap& localToScene) {
	PrefabDescriptor descriptor;
	IdMap sceneToLocal;
	int nextLocal = 1;
	for (auto& object : objects) {
		const int id = Id(object.at("Id"));
		if (!sceneToLocal.emplace(id, nextLocal++).second) {
			throw std::runtime_error("Duplicate object in prefab export");
		}
		if (id == root) {
			object["ParentId"] = -1;
		}
		if (IsPrefabRef(object)) {
			descriptor.PrefabVersion = 2;
		}
	}
	for (const auto& object : objects) {
		for (const int id : OwnedIds(object)) {
			if (!sceneToLocal.contains(id)) {
				sceneToLocal.emplace(id, nextLocal++);
			}
		}
	}
	localToScene.clear();
	for (const auto& [runtime, local] : sceneToLocal) {
		localToScene.emplace(local, runtime);
	}
	for (auto& object : objects) {
		if (IsPrefabRef(object)) {
			RemapPrefabEntry(object, sceneToLocal);
		}
		else {
			Remap(object, sceneToLocal);
		}
		descriptor.Objects.push_back(std::move(object));
	}
	descriptor.RootId = MapId(root, sceneToLocal);
	ValidateAssetObjects(descriptor.Objects, descriptor.RootId, descriptor.PrefabVersion);
	return serde::serialize<Json>(descriptor);
}

Json Compact(const Json& objects, const std::vector<Instance>& instances) {
	Validate(objects);
	const auto current = IndexObjects(objects);
	std::map<int, Json> entries;
	std::set<int> members;
	for (const auto& instance : instances) {
		auto entry = instance.entry;
		const int root = Id(entry.at("Id"));
		if (instance.baseline.empty()) {
			throw std::runtime_error("Missing prefab baseline");
		}
		const auto owned = OwnedIds(entry);
		for (const int id : owned) {
			if (!members.insert(id).second) {
				throw std::runtime_error("Overlapping prefab instances");
			}
		}
		for (const auto& object : objects) {
			if (!owned.contains(Id(object.at("Id"))) && owned.contains(object.value("ParentId", -1))) {
				throw std::runtime_error("Unpack prefab before adding children to an instance");
			}
		}
		entry["ParentId"] = current.at(root)->value("ParentId", -1);
		std::set<int> processed;
		CompactOverrides(entry, IndexObjects(instance.baseline), current, processed, root);
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
}
