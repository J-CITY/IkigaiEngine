#include "sceneDocument.h"
#include "utilsModule/serialization/jsonValueAdaptor.h"
#include <charconv>
#include <serdepp/serde.hpp>
#include <string_view>

namespace IKIGAI::SCENE_SYSTEM::DOCUMENT {
namespace {
	constexpr std::size_t MaxPrefabDepth = 64;

	struct RenderDescriptor {
		std::string RenderPipeline = {};
		template <class Context> constexpr static void serde(Context& context, RenderDescriptor& value) {
			serde::serde_struct(context, value)
				.field(&RenderDescriptor::RenderPipeline, "RenderPipeline", serde::attribute::default_ { std::string {} });
		}
	};

	struct SceneDescriptor {
		std::vector<Json> Objects = {};
		template <class Context> constexpr static void serde(Context& context, SceneDescriptor& value) {
			serde::serde_struct(context, value).field(&SceneDescriptor::Objects, "Objects");
		}
	};
	struct ObjectDescriptor {
		int Id = 0;
		int ParentId = -1;
		std::string Name = {};
		std::string Tag = {};
		bool IsActive = true;
		bool IsVisible = true;
		std::vector<Json> Components = {};
		template <class Context> constexpr static void serde(Context& context, ObjectDescriptor& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&ObjectDescriptor::Id, "Id")
				.field(&ObjectDescriptor::ParentId, "ParentId", default_ { -1 })
				.field(&ObjectDescriptor::Name, "Name", default_ { std::string {} })
				.field(&ObjectDescriptor::Tag, "Tag", default_ { std::string {} })
				.field(&ObjectDescriptor::IsActive, "IsActive", default_ { true })
				.field(&ObjectDescriptor::IsVisible, "IsVisible", default_ { true })
				.field(&ObjectDescriptor::Components, "Components");
		}
	};
	struct PrefabDescriptor {
		int PrefabVersion = 2;
		int RootId = 0;
		std::vector<Json> Objects = {};

		template <class Context> constexpr static void serde(Context& context, PrefabDescriptor& value) {
			serde::serde_struct(context, value)
				.field(&PrefabDescriptor::PrefabVersion, "PrefabVersion")
				.field(&PrefabDescriptor::RootId, "RootId")
				.field(&PrefabDescriptor::Objects, "Objects");
		}
	};

	struct ReferenceDescriptor {
		std::string Prefab = {};
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

	struct OverrideMetadata {
		std::map<std::string, Json> Overrides = {};
		Json RootOverrides = Json::object();

		template <class Context> constexpr static void serde(Context& context, OverrideMetadata& value) {
			using serde::attribute::default_;
			serde::serde_struct(context, value)
				.field(&OverrideMetadata::Overrides, "Overrides", default_ { std::map<std::string, Json> {} })
				.field(&OverrideMetadata::RootOverrides, "RootOverrides", default_ { Json::object() });
		}
	};

	int LocalId(const std::string& key, const char* field) {
		int local = 0;
		const auto end = key.data() + key.size();
		const auto parsed = std::from_chars(key.data(), end, local);
		if (parsed.ec != std::errc {} || parsed.ptr != end || key.front() == '0' || local <= 0 || local >= std::numeric_limits<int>::max()) {
			throw std::runtime_error("Invalid local ID in " + std::string(field) + ": " + key);
		}
		return local;
	}

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

	void ValidateOverride(const Json& patch, bool legacyRoot = false) {
		if (!patch.is_object()) {
			throw std::runtime_error("Object override must be an object");
		}
		for (const auto& [key, value] : patch.items()) {
			if (key == "Name" || key == "Tag") {
				if (!value.is_string()) {
					throw std::runtime_error(key + " override must be a string");
				}
			}
			else if (key == "IsActive" || key == "IsVisible") {
				if (!value.is_boolean()) {
					throw std::runtime_error(key + " override must be a boolean");
				}
			}
			else if (key == "Transform") {
				if (!value.is_object()) {
					throw std::runtime_error("Transform override must be an object");
				}
				for (const auto& [field, vector] : value.items()) {
					if (field != "LocalPosition" && field != "LocalRotation" && field != "LocalScale") {
						throw std::runtime_error("Unsupported Transform override: " + field);
					}
					for (const char* axis : { "X", "Y", "Z" }) {
						if (!vector.contains(axis) || !vector.at(axis).is_number()) {
							throw std::runtime_error("Expected numeric " + field + "." + axis + " in Transform override");
						}
					}
				}
			}
			else if (key == "Components" && !legacyRoot) {
				if (!value.is_object()) {
					throw std::runtime_error("Components override must be an object");
				}
				for (const auto& [type, fields] : value.items()) {
					if (!fields.is_object() || fields.contains(type)) {
						throw std::runtime_error("Expected override fields for component: " + type);
					}
				}
			}
			else {
				throw std::runtime_error("Unsupported object override: " + key);
			}
		}
	}

	void ValidatePatchFields(const Json& entry) {
		if (const auto overrides = entry.find("Overrides"); overrides != entry.end()) {
			if (!overrides->is_object()) {
				throw std::runtime_error("Overrides must be an object");
			}
			for (const auto& [key, patch] : overrides->items()) {
				LocalId(key, "Overrides");
				if (!patch.is_object()) {
					throw std::runtime_error("Overrides[" + key + "] must be an object");
				}
				ValidateOverride(patch);
			}
		}
		if (const auto legacy = entry.find("RootOverrides"); legacy != entry.end()) {
			ValidateOverride(*legacy, true);
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
		for (const auto& [key, child] : NestedInstances(entry).items()) {
			LocalId(key, "NestedInstances");
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
		auto descriptor = serde::deserialize<PrefabDescriptor>(asset);
		// v2 adds nested references; v1 flat objects retain their exact payload.
		// Validate the old schema before upgrading so v1 cannot smuggle v2 fields.
		if (descriptor.PrefabVersion == 1) {
			for (const auto& object : descriptor.Objects) {
				if (IsPrefabRef(object)) {
					throw std::runtime_error("Nested prefabs require PrefabVersion 2");
				}
			}
			descriptor.PrefabVersion = 2;
		}
		return descriptor;
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
				ValidateObject(object);
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

	template <class Visit>
	void VisitLayerOverrides(const std::vector<Json>& layers, int localRoot, const IdMap& live, const std::map<int, Json*>& objects, Visit visit) {
		for (const auto& layer : layers) {
			Json source = Json::object();
			for (const char* field : { "Overrides", "RootOverrides" }) {
				if (layer.contains(field)) {
					source[field] = layer.at(field);
				}
			}
			if (source.empty()) {
				continue;
			}
			auto metadata = serde::deserialize<OverrideMetadata>(source);
			if (!metadata.RootOverrides.empty()) {
				auto root = metadata.Overrides.try_emplace(std::to_string(localRoot), Json::object()).first;
				MergeLegacyRootOverrides(root->second, metadata.RootOverrides);
			}
			for (auto& [key, patch] : metadata.Overrides) {
				const int local = LocalId(key, "Overrides");
				const auto mapped = live.find(local);
				if (mapped != live.end()) {
					visit(*objects.at(mapped->second), std::move(patch));
				}
				// A saved mapping identifies a member removed by a newer prefab. Compact drops its orphaned edits.
				else if (!layer.contains("IdMap") || !layer.at("IdMap").contains(key)) {
					throw std::runtime_error("Unknown local ID in Overrides: " + key + " for " + layer.at("Prefab").get<std::string>());
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

IdMap ReadMap(const Json& entry) {
	IdMap ids;
	if (const auto map = entry.find("IdMap"); map != entry.end()) {
		if (!map->is_object()) {
			throw std::runtime_error("IdMap must be an object");
		}
		for (const auto& [key, value] : map->items()) {
			ids.emplace(LocalId(key, "IdMap"), Id(value));
		}
	}
	return ids;
}

void MergeLegacyRootOverrides(Json& patch, const Json& legacy) {
	ValidateOverride(legacy, true);
	for (auto it = legacy.begin(); it != legacy.end(); ++it) {
		if (it.key() == "Transform") {
			if (!patch.contains("Transform")) {
				patch["Transform"] = Json::object();
			}
			for (auto p = it.value().begin(); p != it.value().end(); ++p) {
				patch["Transform"][p.key()] = p.value();
			}
		}
		else {
			patch[it.key()] = it.value();
		}
	}
}

Json OverridesForLocal(const Json& entry, int localId, int localRoot) {
	ValidatePatchFields(entry);
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

void ApplyObjectOverrides(Json& object, const Json& overrides) {
	ValidateOverride(overrides);
	for (auto it = overrides.begin(); it != overrides.end(); ++it) {
		if (it.key() == "Transform") {
			Transform(object).update(it.value());
		}
		else if (it.key() == "Components") {
			for (auto c = it.value().begin(); c != it.value().end(); ++c) {
				Json* target = nullptr;
				for (auto& component : object.at("Components")) {
					if (ComponentTypeKey(component) == c.key()) {
						target = &component;
						break;
					}
				}
				if (!target) {
					throw std::runtime_error("Override targets missing component: " + c.key());
				}
				target->update(c.value());
			}
		}
		else {
			object[it.key()] = it.value();
		}
	}
}

std::string ReadRenderPipeline(const Json& document) {
	if (!document.is_object()) {
		throw std::runtime_error("Scene document must be an object");
	}
	Json metadata = Json::object();
	if (const auto pipeline = document.find("RenderPipeline"); pipeline != document.end()) {
		if (!pipeline->is_string()) {
			throw std::runtime_error("Scene RenderPipeline must be a string");
		}
		metadata["RenderPipeline"] = *pipeline;
	}
	return serde::deserialize<RenderDescriptor>(metadata).RenderPipeline;
}

void WriteRenderPipeline(Json& document, const std::string& pipeline) {
	if (pipeline.empty()) {
		document.erase("RenderPipeline");
		return;
	}
	document.update(serde::serialize<Json>(RenderDescriptor { pipeline }));
}

std::string ComponentTypeKey(const Json& component) {
	constexpr std::string_view suffix = "Type";
	const std::string* typeKey = nullptr;
	for (auto it = component.begin(); it != component.end(); ++it) {
		const std::string_view key = it.key();
		if (key.size() <= suffix.size() || !key.ends_with(suffix) || !it->is_string()) {
			continue;
		}
		const std::string_view identifier = it->get_ref<const std::string&>();
		const auto qualifier = identifier.rfind("::");
		const auto name = qualifier == std::string_view::npos ? identifier : identifier.substr(qualifier + 2);
		// ComponentCatalog writes <name>Type with either the stable name or
		// its legacy qualified identifier. Other fields may also end in Type.
		if (name != key.substr(0, key.size() - suffix.size())) {
			continue;
		}
		if (typeKey) {
			throw std::runtime_error("Component has multiple type tags");
		}
		typeKey = &it.key();
	}
	if (!typeKey) {
		throw std::runtime_error("Component has no type tag");
	}
	return *typeKey;
}

void ValidateObject(const Json& object) {
	Id(object.at("Id"));
	if (object.contains("ParentId")
		&& (!object.at("ParentId").is_number_integer() || object.at("ParentId") < -1 || object.at("ParentId") >= std::numeric_limits<int>::max())) {
		throw std::runtime_error("Invalid ParentId");
	}
	if (!object.at("Components").is_array()) {
		throw std::runtime_error("Components must be an array");
	}
	auto source = object;
	const auto descriptor = serde::deserialize<ObjectDescriptor>(source);
	for (const auto& component : descriptor.Components) {
		if (!component.is_object()) {
			throw std::runtime_error("Component must be an object");
		}
		ComponentTypeKey(component);
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
	std::map<int, Json*> objects;
	for (auto& object : result.objects) {
		objects.emplace(Id(object.at("Id")), &object);
	}
	// A nested asset's saved IdMap can still name a descendant removed from its source prefab.
	// Only expanded objects belong to the normalized instance and its save baseline.
	std::erase_if(live, [&](const auto& mapping) { return !objects.contains(mapping.second); });
	VisitLayerOverrides(defaults, localRoot, live, objects, [](Json& object, Json patch) { ApplyObjectOverrides(object, patch); });
	VisitLayerOverrides(
		edits, localRoot, live, objects, [&](Json& object, Json patch) { deferredOverrides[Id(object.at("Id"))].push_back(std::move(patch)); });
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
	if (!document.at("Objects").is_array()) {
		throw std::runtime_error("Scene Objects must be an array");
	}
	auto source = document;
	const auto descriptor = serde::deserialize<SceneDescriptor>(source);
	Resolver resolver(descriptor.Objects);
	Expanded result;
	for (auto entry : descriptor.Objects) {
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
	auto document = serde::serialize<Json>(SceneDescriptor { result.get<std::vector<Json>>() });
	if (result.empty()) {
		document["Objects"] = Json::array();
	}
	return document;
}
}
