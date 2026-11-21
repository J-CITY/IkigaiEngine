#pragma once

#include "animationTimelineBind.h"
#include "coreModule/ecs/object.h"
#include "coroutineModule/task.h"
#include "reflectionModule/reflection.h"
#include "resourceModule/jsonLoader.h"
#include "utilsModule/serialization/typedDescriptor.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IKIGAI::SCENE_SYSTEM {
struct SceneLoadState;
}

namespace IKIGAI::ANIMATION {
class AnimationProperty;
}

namespace IKIGAI::ECS {

class VirtualMeshRenderer;

// Closed std::variant lists cannot grow when a component is added.
// Each component translation unit calls enroll<T>(), which records the operations
// the old type list performed: storage, scene IO, references and reflection.
class ComponentCatalog {
public:
	using PrepareFn = std::function<COROUTINE::Task<>(
		const ComponentBase::Descriptor&, std::shared_ptr<SCENE_SYSTEM::SceneLoadState>, std::vector<std::shared_ptr<void>>&)>;

	struct Record {
		std::string name;
		std::string typeName;
		// Serialized discriminator key. Keep stable across C++ type renames.
		std::string typeKey;
		std::type_index type = typeid(void);
		StorageType storage = StorageType::Sparse;
		bool lightweight = false;
		std::function<void(World&)> registerStorage;
		std::function<std::shared_ptr<const ComponentBase::Descriptor>(const nlohmann::json&)> read;
		std::function<void(Object&, const ComponentBase::Descriptor&)> apply;
		std::function<nlohmann::json(const ComponentBase&)> write;
		std::function<void(Object&)> create;
		std::function<void(Object&)> remove;
		std::function<UTILS::WeakPtr<ComponentBase>(Object&)> resolve;
		PrepareFn prepare;
		std::function<bool(World&, Object::Id, std::string_view, std::string_view, ANIMATION::AnimationProperty&)> bindTimelineProperty;
	};

	static ComponentCatalog& instance();

	// storage: where the component lives. lightweight: allowed in a loading scene.
	// The returned bool is only there so a static initializer can call enroll.
	template <class T> static bool enroll(StorageType storage = StorageType::Sparse, bool lightweight = false);

	void applyStorage(World& world) const;
	void setPrepare(std::type_index type, PrepareFn prepare);
	const Record* findByName(std::string_view name) const;
	const Record* findByType(std::type_index type) const;
	const Record* findInJson(const nlohmann::json& component) const;
	std::vector<std::string> typeNames() const;
	void createOnObject(Object& object, std::string_view typeName) const;
	void removeFromObject(Object& object, std::type_index type) const;
	UTILS::WeakPtr<ComponentBase> resolveOnObject(Object& object, std::string_view name) const;
	bool tryBindTimelineProperty(World& world, Object::Id id, std::string_view componentName, std::string_view propertyName,
		std::string_view trackName, ANIMATION::AnimationProperty& out) const;

private:
	void add(Record record);
	const Record* find(const std::unordered_map<std::string, std::size_t>& index, std::string_view key) const;

	std::vector<Record> mRecords;
	std::unordered_map<std::string, std::size_t> mByName;
	std::unordered_map<std::string, std::size_t> mByTypeName;
	std::unordered_map<std::string, std::size_t> mByTypeKey;
	std::unordered_map<std::type_index, std::size_t> mByType;
};

template <class T> bool ComponentCatalog::enroll(StorageType storage, bool lightweight) {
	Record record;
	record.name = GetComponentName<T>();
	record.typeName = GetType<T>();
	record.typeKey = record.name + "Type";
	record.type = typeid(T);
	record.storage = storage;
	record.lightweight = lightweight;
	if (record.name.empty() || record.typeName.empty()) {
		throw std::logic_error("Component registration requires GetComponentName and GetType");
	}
	record.registerStorage = [storage](World& world) { world.getComponentManager()->template registerComponent<T>(storage); };
	record.read = [](const nlohmann::json& component) -> std::shared_ptr<const ComponentBase::Descriptor> {
		auto document = component;
		// GetComponentName is an explicit persistent ID; GetType is the legacy alias.
		document[GetComponentName<T>() + "Type"] = GetType<T>();
		if constexpr (std::is_same_v<T, SurfaceComponent> || std::is_same_v<T, FoliageComponent> || std::is_same_v<T, ChunkModelRenderer>
			|| std::is_same_v<T, VirtualMeshRenderer>) {
			return std::make_shared<typename T::Descriptor>(DATA::readDescriptor<typename T::Descriptor>(std::move(document)));
		}
		else {
			return std::make_shared<typename T::Descriptor>(serde::deserialize<typename T::Descriptor>(document));
		}
	};
	record.apply = [](Object& object, const ComponentBase::Descriptor& descriptor) { object.template addComponent<T>(descriptor); };
	record.write = [](const ComponentBase& component) {
		auto document = serde::serialize<nlohmann::json>(static_cast<const T&>(component).getDescriptor());
		document[GetComponentName<T>() + "Type"] = GetComponentName<T>();
		return document;
	};
	record.create = [](Object& object) { object.template addComponent<T>(); };
	record.remove = [](Object& object) { object.template removeComponent<T>(); };
	record.resolve = [](Object& object) -> UTILS::WeakPtr<ComponentBase> {
		auto component = object.template getComponent<T>();
		return component ? UTILS::WeakPtr<ComponentBase>(static_cast<ComponentBase*>(component.get())) : nullptr;
	};

	auto& reflection = UTILS::ReflectionManager::Instance();
	reflection.registerType<T>();
	reflection.addTypeToGroup<T>("Component");
	reflection.registerStaticField<T, std::string>("ClassName", std::function<std::string()>([] { return GetComponentName<T>(); }), {});
	reflection.registerStaticMethod<T>("AddComponent", +[](std::shared_ptr<Object> object) { object->template addComponent<T>(); });
	reflection.registerStaticMethod<T>("RemoveComponent", +[](std::shared_ptr<Object> object) { object->template removeComponent<T>(); });
	if (auto* type = reflection.getType<T>()) {
		UTILS::MethodInfo configuration;
		configuration.name = "Configuration";
		configuration.invoke = [](void* instance, const std::vector<UTILS::Any>&) -> UTILS::Any {
			auto json = UTILS::ToJson(static_cast<T*>(instance)->getDescriptor());
			if (!json.isOk()) {
				throw std::runtime_error("Component configuration serialization failed");
			}
			return json.unwrap();
		};
		type->mMethods[configuration.name] = std::move(configuration);
	}
	record.bindTimelineProperty
		= [](World& world, Object::Id id, std::string_view propertyName, std::string_view trackName, ANIMATION::AnimationProperty& out) {
			  return ANIMATION::detail::bindTimelineFromMembers<T>(world, id, propertyName, trackName, out);
		  };
	instance().add(std::move(record));
	return true;
}
}
