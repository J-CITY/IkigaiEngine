#include "componentReference.inl"
#include "componentTypes.h"
#include "utilsModule/reflection/reflection.h"
#include <map>

namespace IKIGAI::ECS {
namespace {
	using Resolver = UTILS::WeakPtr<ComponentBase> (*)(Object&);
	template <class... T> auto resolvers(ComponentsTypeProvider<T...>) {
		return std::map<std::string, Resolver> { { GetComponentName<T>(), +[](Object& o) -> UTILS::WeakPtr<ComponentBase> {
													  auto component = o.getComponent<T>();
													  return component ? UTILS::WeakPtr<ComponentBase>(static_cast<ComponentBase*>(component.get()))
																	   : nullptr;
												  } }... };
	}
	auto& types() {
		static auto r = resolvers(ComponentsTypeProviderType {});
		return r;
	}
	auto& properties() {
		static std::map<std::pair<std::string, std::string>, PropertyRegistry::Accessor> r;
		return r;
	}
}
UTILS::WeakPtr<ComponentBase> ResolveComponent(SCENE_SYSTEM::Scene& scene, const ComponentReference& r) {
	if (r.empty()) {
		return nullptr;
	}
	auto it = types().find(r.Component);
	if (it == types().end()) {
		throw std::runtime_error("Unknown component reference type: " + r.Component);
	}
	auto object = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(r.ObjectId)));
	return object ? it->second(*object) : nullptr;
}
DATA::Value PropertyBinding::read(SCENE_SYSTEM::Scene& scene) const {
	if (!handle) {
		handle = ResolveComponent(scene, target);
	}
	if (!handle) {
		throw std::runtime_error("Missing property target: object " + std::to_string(target.ObjectId) + " / " + target.Component);
	}
	return getter(*handle.get());
}
void PropertyRegistry::add(const std::string& c, const std::string& p, Accessor a) {
	properties().insert_or_assign({ c, p }, std::move(a));
}
PropertyBinding PropertyRegistry::bind(SCENE_SYSTEM::Scene& scene, const PropertyReference& r, DATA::ValueType type) {
	auto handle = ResolveComponent(scene, r.Target);
	if (!handle) {
		throw std::runtime_error("Missing bound component: " + r.Target.Component + " on " + std::to_string(r.Target.ObjectId));
	}
	auto it = properties().find({ r.Target.Component, r.Property });
	if (it == properties().end()) {
		// Registered reflection getters are explicit property exports. Private
		// unreflected fields remain inaccessible; setters are never exposed here.
		auto* info = UTILS::ReflectionManager::Instance().getType(r.Target.Component);
		if (!info || !info->mFields.contains(r.Property)) {
			throw std::runtime_error("Property is not exported for binding: " + r.Target.Component + "." + r.Property);
		}
		const auto field = info->mFields.at(r.Property);
		if (!field.getter) {
			throw std::runtime_error("Bound property has no getter: " + r.Property);
		}
		DATA::ValueType actual;
		if (field.typeId == UTILS::ReflectionManager::GetTypeId<float>()) {
			actual = DATA::ValueType::Float;
		}
		else if (field.typeId == UTILS::ReflectionManager::GetTypeId<int>()) {
			actual = DATA::ValueType::Int;
		}
		else if (field.typeId == UTILS::ReflectionManager::GetTypeId<bool>()) {
			actual = DATA::ValueType::Bool;
		}
		else if (field.typeId == UTILS::ReflectionManager::GetTypeId<MATH::Vector3f>()) {
			actual = DATA::ValueType::Vector3;
		}
		else {
			throw std::runtime_error("Unsupported bound property type: " + r.Property);
		}
		add(r.Target.Component, r.Property, { actual, [field, actual](ComponentBase& component) -> DATA::Value {
												 auto value = field.getter(dynamic_cast<void*>(&component));
												 switch (actual) {
												 case DATA::ValueType::Float:
													 return UTILS::any_cast<float>(value);
												 case DATA::ValueType::Int:
													 return UTILS::any_cast<int>(value);
												 case DATA::ValueType::Bool:
													 return UTILS::any_cast<bool>(value);
												 case DATA::ValueType::Vector3:
													 return UTILS::any_cast<MATH::Vector3f>(value);
												 case DATA::ValueType::EntityRef:
													 break;
												 }
												 throw std::runtime_error("Invalid reflected property type");
											 } });
		it = properties().find({ r.Target.Component, r.Property });
	}
	if (it->second.type != type) {
		throw std::runtime_error("Property binding type mismatch: " + r.Property);
	}
	return { r.Target, type, it->second.get, handle };
}
namespace {
	template <class T> void registerSceneComponent() {
		auto& reflection = UTILS::ReflectionManager::Instance();
		reflection.registerType<T>();
		reflection.addTypeToGroup<T>("Component");
		reflection.registerStaticField<T, std::string>("ClassName", std::function<std::string()>([] { return GetComponentName<T>(); }), {});
		reflection.registerStaticMethod<T>("AddComponent", +[](std::shared_ptr<Object> object) { object->addComponent<T>(); });
		reflection.registerStaticMethod<T>("RemoveComponent", +[](std::shared_ptr<Object> object) { object->removeComponent<T>(); });
		auto* type = reflection.getType<T>();
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
}
void RegisterSceneComponentReflection() {
	registerSceneComponent<BehaviorTreeComponent>();
	registerSceneComponent<BlackboardComponent>();
	registerSceneComponent<VariableContextComponent>();
	registerSceneComponent<SkeletalStateGraphComponent>();
	registerSceneComponent<FacialAnimationComponent>();
	registerSceneComponent<SurfaceComponent>();
	registerSceneComponent<FoliageComponent>();
	registerSceneComponent<ChunkModelRenderer>();
	registerSceneComponent<VirtualMeshRenderer>();
	UTILS::ReflectionManager::Instance().registerMethod("SetWeight", &FacialAnimationComponent::setWeight);
	UTILS::ReflectionManager::Instance().registerMethod("Play", &FacialAnimationComponent::play);
	UTILS::ReflectionManager::Instance().registerMethod("ClearWeights", &FacialAnimationComponent::clearWeights);
#if USE_PHYSICS_3D
	registerSceneComponent<CapsuleColliderComponent>();
	registerSceneComponent<CharacterMotorComponent>();
	registerSceneComponent<ThirdPersonController>();
	registerSceneComponent<ThirdPersonCameraComponent>();
	registerSceneComponent<TraversalComponent>();
	registerSceneComponent<RagdollComponent>();
	registerSceneComponent<DestructibleMeshComponent>();
	UTILS::ReflectionManager::Instance().registerMethod("Slice", &DestructibleMeshComponent::slice);
	UTILS::ReflectionManager::Instance().registerMethod("Reset", &DestructibleMeshComponent::reset);
	UTILS::ReflectionManager::Instance().registerMethod("Activate", &RagdollComponent::activate);
	UTILS::ReflectionManager::Instance().registerMethod("Reset", &RagdollComponent::reset);
	UTILS::ReflectionManager::Instance().registerMethod("ApplyImpulse", &RagdollComponent::applyImpulse);
#ifdef IKIGAI_TEST_MODULE
	registerSceneComponent<CharacterDiagnosticsComponent>();
#endif
#endif
}

}
