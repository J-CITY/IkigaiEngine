#include "componentCatalog.h"

#include "utilsModule/animation.h"

#include <algorithm>

namespace IKIGAI::ECS {
ComponentCatalog& ComponentCatalog::instance() {
	static ComponentCatalog catalog;
	return catalog;
}

void ComponentCatalog::add(Record record) {
	if (mByName.contains(record.name) || mByTypeKey.contains(record.typeKey) || mByTypeName.contains(record.typeName)
		|| mByType.contains(record.type)) {
		throw std::logic_error("Duplicate component registration: " + record.name);
	}
	const auto index = mRecords.size();
	mByName.emplace(record.name, index);
	mByTypeName.emplace(record.typeName, index);
	mByTypeKey.emplace(record.typeKey, index);
	mByType.emplace(record.type, index);
	mRecords.push_back(std::move(record));
}

const ComponentCatalog::Record* ComponentCatalog::find(const std::unordered_map<std::string, std::size_t>& index, std::string_view key) const {
	const auto found = index.find(std::string(key));
	if (found == index.end()) {
		return nullptr;
	}
	return &mRecords[found->second];
}

const ComponentCatalog::Record* ComponentCatalog::findByName(std::string_view name) const {
	return find(mByName, name);
}

const ComponentCatalog::Record* ComponentCatalog::findByType(std::type_index type) const {
	const auto found = mByType.find(type);
	return found != mByType.end() ? &mRecords[found->second] : nullptr;
}

void ComponentCatalog::applyStorage(World& world) const {
	for (const auto& record : mRecords) {
		record.registerStorage(world);
	}
}

void ComponentCatalog::setPrepare(std::type_index type, PrepareFn prepare) {
	const auto found = mByType.find(type);
	if (found == mByType.end()) {
		throw std::logic_error("Cannot prepare an unregistered component: " + std::string(type.name()));
	}
	mRecords[found->second].prepare = std::move(prepare);
}

const ComponentCatalog::Record* ComponentCatalog::findInJson(const nlohmann::json& component) const {
	if (!component.is_object()) {
		throw std::runtime_error("Component descriptor must be an object");
	}
	const Record* found = nullptr;
	for (const auto& item : component.items()) {
		const auto* record = find(mByTypeKey, item.key());
		if (!record) {
			continue;
		}
		if (found) {
			throw std::runtime_error("Component descriptor has multiple type keys");
		}
		found = record;
	}
	return found;
}

void ComponentCatalog::read(Object& object, const nlohmann::json& component) const {
	const auto* record = findInJson(component);
	if (!record) {
		throw std::runtime_error("Component descriptor has no supported type key");
	}
	record->apply(object, component);
}

std::vector<std::string> ComponentCatalog::typeNames() const {
	std::vector<std::string> names;
	names.reserve(mRecords.size());
	for (const auto& record : mRecords) {
		names.push_back(record.typeName);
	}
	std::sort(names.begin(), names.end());
	return names;
}

void ComponentCatalog::createOnObject(Object& object, std::string_view typeName) const {
	const auto* record = find(mByTypeName, typeName);
	if (!record) {
		record = find(mByName, typeName);
	}
	if (record) {
		record->create(object);
	}
}

void ComponentCatalog::removeFromObject(Object& object, std::type_index type) const {
	if (const auto* record = findByType(type)) {
		record->remove(object);
	}
}

UTILS::WeakPtr<ComponentBase> ComponentCatalog::resolveOnObject(Object& object, std::string_view name) const {
	const auto* record = find(mByName, name);
	if (!record) {
		return nullptr;
	}
	return record->resolve(object);
}

bool ComponentCatalog::tryBindTimelineProperty(World& world, Object::Id id, std::string_view componentName, std::string_view propertyName,
	std::string_view trackName, ANIMATION::AnimationProperty& out) const {
	if (const auto* record = findByName(componentName); record && record->bindTimelineProperty) {
		return record->bindTimelineProperty(world, id, propertyName, trackName, out);
	}
	return false;
}

#ifdef USE_EDITOR
void ComponentCatalog::inspectComponent(UTILS::WeakPtr<ComponentBase> component) const {
	if (!component) {
		return;
	}
	if (const auto* record = findByType(typeid(*component)); record && record->inspect) {
		record->inspect(component);
	}
}

void ComponentCatalog::collectTimeline(const std::string& name, Object::Id id, UTILS::WeakPtr<ComponentBase> component) const {
	if (!component) {
		return;
	}
	if (const auto* record = findByType(typeid(*component)); record && record->collectTimeline) {
		record->collectTimeline(name, id, component);
	}
}

void ComponentCatalog::bindTimeline(
	const std::string& name, Object::Id id, const std::string& componentName, const std::string& propertyName, void* prop) const {
	if (const auto* record = findByName(componentName)) {
		if (record->bindTimeline) {
			record->bindTimeline(name, id, componentName, propertyName, prop);
		}
	}
}
#endif
}
