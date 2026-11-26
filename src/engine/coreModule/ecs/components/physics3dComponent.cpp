#include "physics3dComponent.h"
#if USE_PHYSICS_3D
#include "collisionModule/compoundCollider.h"
#include "coreModule/ecs/object.h"
using namespace IKIGAI;
using namespace IKIGAI::ECS;
Physics3DComponent::Physics3DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "Physics3DComponent";
	settings.Type = GetType<Physics3DComponent>();
}
Physics3DComponent::Physics3DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: Physics3DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<Physics3DComponent>();
}
PHYSICS::Shape Physics3DComponent::colliderShape() const {
	if (settings.ShapeKind == 0) {
		return PHYSICS::Shape::sphere(settings.Radius);
	}
	if (settings.ShapeKind == 1) {
		return PHYSICS::Shape::box(settings.HalfSize);
	}
	if (settings.ShapeKind != 2 || settings.BodyMode != 0) {
		throw std::invalid_argument("Expected a sphere, box or static compound collider");
	}
	std::vector<COLLISION::CompoundCollider3D::Child> children;
	for (const auto& box : settings.Boxes) {
		const MATH::QuaternionF rotation(box.Rotation);
		children.push_back({ PHYSICS::Shape::box(box.HalfSize), box.Position,
			{ rotation * MATH::Vector3f { 1, 0, 0 }, rotation * MATH::Vector3f { 0, 1, 0 }, rotation * MATH::Vector3f { 0, 0, 1 } } });
	}
	return PHYSICS::Shape::compound(std::make_shared<COLLISION::CompoundCollider3D>(std::move(children)));
}
SoftBody3DComponent::SoftBody3DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "SoftBody3DComponent";
	settings.Type = GetType<SoftBody3DComponent>();
}
SoftBody3DComponent::SoftBody3DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: SoftBody3DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<SoftBody3DComponent>();
}
Cloth3DComponent::Cloth3DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "Cloth3DComponent";
	settings.Type = GetType<Cloth3DComponent>();
}
Cloth3DComponent::Cloth3DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: Cloth3DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<Cloth3DComponent>();
}
Fluid3DComponent::Fluid3DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "Fluid3DComponent";
	settings.Type = GetType<Fluid3DComponent>();
}
Fluid3DComponent::Fluid3DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: Fluid3DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<Fluid3DComponent>();
}
ForceField3DComponent::ForceField3DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "ForceField3DComponent";
	settings.Type = GetType<ForceField3DComponent>();
}
ForceField3DComponent::ForceField3DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ForceField3DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<ForceField3DComponent>();
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Physics3DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SoftBody3DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Cloth3DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Fluid3DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ForceField3DComponent>();
	return true;
}();
}

#endif // USE_PHYSICS_3D
