#include "physicsComponent.h"
#if USE_PHYSICS_3D

#include "transform.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

PhysicsComponent::PhysicsComponent(UTILS::Ref<ECS::Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "PhysicsComponent";
}

PhysicsComponent::Descriptor PhysicsComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<PhysicsComponent>();
	return descriptor;
}
// #include <rttr/registration>
//
// RTTR_REGISTRATION
//{
//	rttr::registration::enumeration<IKIGAI::ECS::CollisionType>("CollisionType")
//	(
//		rttr::value("NONE", IKIGAI::ECS::CollisionType::NONE),
//		rttr::value("SPHERE", IKIGAI::ECS::CollisionType::SPHERE),
//		rttr::value("AABB", IKIGAI::ECS::CollisionType::OBB),
//		rttr::value("OBB", IKIGAI::ECS::CollisionType::OBB),
//		rttr::value("COUNT", IKIGAI::ECS::CollisionType::COUNT)
//	);
//
//	rttr::registration::class_<IKIGAI::ECS::PhysicsComponent>("PhysicsComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
// }


#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::PhysicsComponent>();
	return true;
}();
}

#endif // USE_PHYSICS_3D
