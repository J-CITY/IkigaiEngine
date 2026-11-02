#pragma once

#ifdef USE_EDITOR
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "coreModule/ecs/object.h"
#include "coroutineModule/task.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/reflection/reflection.h"
#include "utilsModule/serialization/typedDescriptor.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IKIGAI::SCENE_SYSTEM {
struct SceneLoadState;
}

namespace IKIGAI::ECS {

// Closed std::variant lists cannot grow when a component is added.
// Each component translation unit calls enroll<T>(), which records the operations
// the old type list performed: storage, scene IO, references and reflection.
class ComponentCatalog {
public:
	using PrepareFn
		= std::function<COROUTINE::Task<>(const nlohmann::json&, std::shared_ptr<SCENE_SYSTEM::SceneLoadState>, std::vector<std::shared_ptr<void>>&)>;

	struct Record {
		std::string name;
		std::string typeName;
		std::string typeKey;
		std::string typeIdName;
		StorageType storage = StorageType::Sparse;
		bool lightweight = false;
		std::function<void(World&)> registerStorage;
		std::function<void(Object&, const nlohmann::json&)> apply;
		std::function<nlohmann::json(const ComponentBase&)> write;
		std::function<void(Object&)> create;
		std::function<void(Object&)> remove;
		std::function<UTILS::WeakPtr<ComponentBase>(Object&)> resolve;
		PrepareFn prepare;
#ifdef USE_EDITOR
		std::function<void(UTILS::WeakPtr<ComponentBase>)> inspect;
		std::function<void(const std::string&, Object::Id, UTILS::WeakPtr<ComponentBase>)> collectTimeline;
		std::function<void(const std::string&, Object::Id, const std::string&, const std::string&, void*)> bindTimeline;
#endif
	};

	static ComponentCatalog& instance();

	// storage: where the component lives. lightweight: allowed in a loading scene.
	// The returned bool is only there so a static initializer can call enroll.
	template <class T> static bool enroll(StorageType storage = StorageType::Sparse, bool lightweight = false);

	void applyStorage(World& world) const;
	void setPrepare(std::string_view name, PrepareFn prepare);
	const Record* findByName(std::string_view name) const;
	const Record* findInJson(const nlohmann::json& component) const;
	void read(Object& object, const nlohmann::json& component) const;
	std::vector<std::string> typeNames() const;
	void createOnObject(Object& object, std::string_view typeName) const;
	void removeFromObject(Object& object, std::string_view key) const;
	UTILS::WeakPtr<ComponentBase> resolveOnObject(Object& object, std::string_view name) const;
#ifdef USE_EDITOR
	void inspectComponent(UTILS::WeakPtr<ComponentBase> component) const;
	void collectTimeline(const std::string& name, Object::Id id, UTILS::WeakPtr<ComponentBase> component) const;
	void bindTimeline(const std::string& name, Object::Id id, const std::string& componentName, const std::string& propertyName, void* prop) const;
#endif

private:
	void add(Record record);
	const Record* find(const std::unordered_map<std::string, std::size_t>& index, std::string_view key) const;

	std::vector<Record> mRecords;
	std::unordered_map<std::string, std::size_t> mByName;
	std::unordered_map<std::string, std::size_t> mByTypeName;
	std::unordered_map<std::string, std::size_t> mByTypeKey;
	std::unordered_map<std::string, std::size_t> mByTypeId;
};

#ifdef USE_EDITOR
}
#include "editorModule/componentEditorHooks.h"
namespace IKIGAI::ECS {
#else
template <class T> inline void BindComponentEditor(ComponentCatalog::Record&) { }
#endif

template <class T> bool ComponentCatalog::enroll(StorageType storage, bool lightweight) {
	Record record;
	record.name = GetComponentName<T>();
	record.typeName = GetType<T>();
	record.typeKey = record.name + "Type";
	record.typeIdName = typeid(T).name();
	record.storage = storage;
	record.lightweight = lightweight;
	if (record.name.empty() || record.typeName.empty()) {
		throw std::logic_error("Component registration requires GetComponentName and GetType");
	}
	record.registerStorage = [storage](World& world) { world.getComponentManager()->template registerComponent<T>(storage); };
	record.apply = [](Object& object, const nlohmann::json& component) {
		auto document = component;
		typename T::Descriptor descriptor;
		if constexpr (std::is_same_v<T, SurfaceComponent> || std::is_same_v<T, FoliageComponent> || std::is_same_v<T, ChunkModelRenderer>) {
			descriptor = DATA::readDescriptor<typename T::Descriptor>(std::move(document));
		}
		else {
			descriptor = serde::deserialize<typename T::Descriptor>(document);
		}
		object.template addComponent<T>(static_cast<const ComponentBase::Descriptor&>(descriptor));
	};
	record.write = [](const ComponentBase& component) { return serde::serialize<nlohmann::json>(static_cast<const T&>(component).getDescriptor()); };
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
	BindComponentEditor<T>(record);
	instance().add(std::move(record));
	return true;
}
}
