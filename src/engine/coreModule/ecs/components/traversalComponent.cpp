#include "traversalComponent.h"
#if USE_PHYSICS_3D
namespace IKIGAI::ECS {
TraversalComponent::TraversalComponent(UTILS::Ref<Object> o)
	: ComponentBase(o) {
	__NAME__ = "TraversalComponent";
	settings.Type = "class IKIGAI::ECS::TraversalComponent";
}
TraversalComponent::TraversalComponent(UTILS::Ref<Object> o, const ComponentBase::Descriptor& d)
	: TraversalComponent(o) {
	settings = static_cast<const Descriptor&>(d);
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::TraversalComponent>();
	return true;
}();
}

#endif
