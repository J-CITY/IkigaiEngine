#include "thirdPersonCameraComponent.h"
#if USE_PHYSICS_3D
#include "resourceModule/serviceManager.h"
#include "windowModule/window/window.h"
namespace IKIGAI::ECS {
ThirdPersonCameraComponent::ThirdPersonCameraComponent(UTILS::Ref<Object> o)
	: ComponentBase(o) {
	__NAME__ = "ThirdPersonCameraComponent";
	settings.Type = "class IKIGAI::ECS::ThirdPersonCameraComponent";
}
ThirdPersonCameraComponent::ThirdPersonCameraComponent(UTILS::Ref<Object> o, const ComponentBase::Descriptor& d)
	: ThirdPersonCameraComponent(o) {
	settings = static_cast<const Descriptor&>(d);
}
void ThirdPersonCameraComponent::onDisable() {
	if (captured && RESOURCES::ServiceManager::Check<WINDOW::Window>()) {
		RESOURCES::ServiceManager::Get<WINDOW::Window>().setCursorVisible(true, false);
	}
	captured = false;
}

}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ThirdPersonCameraComponent>();
	return true;
}();
}

#endif
