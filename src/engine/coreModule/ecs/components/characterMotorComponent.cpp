#include "characterMotorComponent.h"
#if USE_PHYSICS_3D
namespace IKIGAI::ECS {
CharacterMotorComponent::CharacterMotorComponent(UTILS::Ref<Object> o)
	: ComponentBase(o) {
	__NAME__ = "CharacterMotorComponent";
	settings.Type = "class IKIGAI::ECS::CharacterMotorComponent";
}
CharacterMotorComponent::CharacterMotorComponent(UTILS::Ref<Object> o, const ComponentBase::Descriptor& d)
	: CharacterMotorComponent(o) {
	settings = static_cast<const Descriptor&>(d);
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::CharacterMotorComponent>();
	return true;
}();
}

#endif
