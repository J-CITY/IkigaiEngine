#include "capsuleColliderComponent.h"
#if USE_PHYSICS_3D
#include <cmath>
#include <stdexcept>
namespace IKIGAI::ECS {
CapsuleColliderComponent::CapsuleColliderComponent(UTILS::Ref<Object> o)
	: ComponentBase(o) {
	__NAME__ = "CapsuleColliderComponent";
	settings.Type = "class IKIGAI::ECS::CapsuleColliderComponent";
}
CapsuleColliderComponent::CapsuleColliderComponent(UTILS::Ref<Object> o, const ComponentBase::Descriptor& d)
	: CapsuleColliderComponent(o) {
	settings = static_cast<const Descriptor&>(d);
}
PHYSICS::CHARACTER::Capsule CapsuleColliderComponent::shape(MATH::Vector3f feet) const {
	if (!std::isfinite(settings.Radius) || !std::isfinite(settings.Height) || settings.Radius <= 0 || settings.Height < 2 * settings.Radius) {
		throw std::runtime_error("Invalid capsule dimensions");
	}
	return { feet, settings.Radius, settings.Height };
}

}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::CapsuleColliderComponent>();
	return true;
}();
}

#endif
