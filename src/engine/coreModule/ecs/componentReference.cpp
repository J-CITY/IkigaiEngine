#include "componentCatalog.h"
#include "componentReference.inl"
#include "utilsModule/reflection/reflection.h"
#include <map>

namespace IKIGAI::ECS {
namespace {
	auto& properties() {
		static std::map<std::pair<std::string, std::string>, PropertyRegistry::Accessor> r;
		return r;
	}
}
UTILS::WeakPtr<ComponentBase> ResolveComponent(SCENE_SYSTEM::Scene& scene, const ComponentReference& r) {
	if (r.empty()) {
		return nullptr;
	}
	if (!ComponentCatalog::instance().findByName(r.Component)) {
		throw std::runtime_error("Unknown component reference type: " + r.Component);
	}
	auto object = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(r.ObjectId)));
	return object ? ComponentCatalog::instance().resolveOnObject(*object, r.Component) : nullptr;
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
void RegisterSceneComponentReflection() {
	auto& reflection = UTILS::ReflectionManager::Instance();
	reflection.registerMethod("SetWeight", &FacialAnimationComponent::setWeight);
	reflection.registerMethod("Play", &FacialAnimationComponent::play);
	reflection.registerMethod("ClearWeights", &FacialAnimationComponent::clearWeights);
#if USE_PHYSICS_3D
	reflection.registerMethod("Slice", &DestructibleMeshComponent::slice);
	reflection.registerMethod("Reset", &DestructibleMeshComponent::reset);
	reflection.registerMethod("Activate", &RagdollComponent::activate);
	reflection.registerMethod("Reset", &RagdollComponent::reset);
	reflection.registerMethod("ApplyImpulse", &RagdollComponent::applyImpulse);
#endif
}

}
