#include "vegetationInteractorComponent.h"
#include "coreModule/ecs/object.h"
#include <cmath>

namespace IKIGAI::ECS {
VegetationInteractorComponent::VegetationInteractorComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "VegetationInteractorComponent";
	settings.Type = GetType<VegetationInteractorComponent>();
}
VegetationInteractorComponent::VegetationInteractorComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor)
	: VegetationInteractorComponent(object) {
	settings = static_cast<const Descriptor&>(descriptor);
	if (!std::isfinite(settings.Radius) || settings.Radius <= 0 || !std::isfinite(settings.Height) || settings.Height < 0
		|| !std::isfinite(settings.Strength) || settings.Strength < 0 || !std::isfinite(settings.RecoverySeconds) || settings.RecoverySeconds < 0) {
		throw std::invalid_argument("Invalid VegetationInteractor dimensions/strength/recovery");
	}
}
VegetationInteractorComponent::Descriptor VegetationInteractorComponent::getDescriptor() const {
	return settings;
}
void VegetationInteractorComponent::onUpdate(std::chrono::duration<double> elapsed) {
	ENVIRONMENT::advanceVegetationInteractionTrail(mTrail, settings, obj->getTransform()->getWorldMatrix(), float(elapsed.count()));
}
void VegetationInteractorComponent::onDisable() {
	mTrail.current = {};
	mTrail.previous = {};
}
const ENVIRONMENT::VegetationInteractionTrail& VegetationInteractorComponent::trail() const {
	return mTrail;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::VegetationInteractorComponent>();
	return true;
}();
}
