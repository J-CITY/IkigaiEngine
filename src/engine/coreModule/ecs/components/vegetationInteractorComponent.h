#pragma once
#include "component.h"
#include <array>

namespace IKIGAI::ECS {
// Explicit visual capsule proxy; attach it to a moving object or bone attachment.
class VegetationInteractorComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = {};
		float Radius = 0.5f;
		float Height = 1.8f;
		float Strength = 1.0f;
		float RecoverySeconds = 0.5f;
		MATH::Vector3f Offset = {};
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
	struct Sample {
		MATH::Vector4f StartRadius = {};
		MATH::Vector4f EndStrength = {};
	};
	explicit VegetationInteractorComponent(UTILS::Ref<Object> object);
	VegetationInteractorComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	static auto GetMembers() {
		return std::tuple {};
	}
	void onUpdate(std::chrono::duration<double> dt) override;
	void onDisable() override;
	const std::array<Sample, 4>& samples() const;
	const std::array<Sample, 4>& previousSamples() const;
	Descriptor settings = {};

private:
	std::array<Sample, 4> mSamples = {};
	std::array<Sample, 4> mPrevious = {};
	float mTrailTime = 0.0f;
};
template <> inline std::string GetType<VegetationInteractorComponent>() {
	return "class IKIGAI::ECS::VegetationInteractorComponent";
}
template <> inline std::string GetComponentName<VegetationInteractorComponent>() {
	return "VegetationInteractorComponent";
}
}
