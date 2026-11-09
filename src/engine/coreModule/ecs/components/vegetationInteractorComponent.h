#pragma once
#include "component.h"
#include "environmentModule/vegetationInteractionTrail.h"

namespace IKIGAI::ECS {
// Explicit visual capsule proxy; attach it to a moving object or bone attachment.
class VegetationInteractorComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor, ENVIRONMENT::VegetationInteractionSettings {
		std::string Type = {};
		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "VegetationInteractorComponentType")
				.field(&Descriptor::Radius, "Radius", default_ { 0.5f })
				.field(&Descriptor::Height, "Height", default_ { 1.8f })
				.field(&Descriptor::Strength, "Strength", default_ { 1.0f })
				.field(&Descriptor::RecoverySeconds, "RecoverySeconds", default_ { 0.5f })
				.field(&Descriptor::Offset, "Offset", default_ { MATH::Vector3f {} });
		}
	};
	explicit VegetationInteractorComponent(UTILS::Ref<Object> object);
	VegetationInteractorComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void onUpdate(std::chrono::duration<double> dt) override;
	void onDisable() override;
	const ENVIRONMENT::VegetationInteractionTrail& trail() const;
	Descriptor settings = {};

private:
	ENVIRONMENT::VegetationInteractionTrail mTrail {};
};
template <> inline std::string GetType<VegetationInteractorComponent>() {
	return "class IKIGAI::ECS::VegetationInteractorComponent";
}
template <> inline std::string GetComponentName<VegetationInteractorComponent>() {
	return "VegetationInteractorComponent";
}
}
