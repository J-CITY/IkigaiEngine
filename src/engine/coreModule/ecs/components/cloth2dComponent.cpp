#include "cloth2dComponent.h"
#if USE_PHYSICS_2D
#include "coreModule/ecs/object.h"
using namespace IKIGAI;
using namespace IKIGAI::ECS;
Cloth2DComponent::Cloth2DComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "Cloth2DComponent";
	settings.Type = GetType<Cloth2DComponent>();
}
Cloth2DComponent::Cloth2DComponent(UTILS::Ref<Object> object, const Descriptor& descriptor)
	: Cloth2DComponent(object) {
	settings = descriptor;
	settings.Type = GetType<Cloth2DComponent>();
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Cloth2DComponent>();
	return true;
}();
}

#endif
