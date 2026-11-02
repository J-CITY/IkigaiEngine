#include "forceField2dComponent.h"
#if USE_PHYSICS_2D
#include "coreModule/ecs/object.h"
using namespace IKIGAI;
using namespace IKIGAI::ECS;
ForceField2DComponent::ForceField2DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "ForceField2DComponent";
	settings.Type = GetType<ForceField2DComponent>();
}
ForceField2DComponent::ForceField2DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: ForceField2DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<ForceField2DComponent>();
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ForceField2DComponent>();
	return true;
}();
}

#endif
