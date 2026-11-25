#pragma once
#include "component.h"
#include "mathModule/math.h"
#include <array>
namespace IKIGAI::RENDER {
class MaterialInterface;
}
namespace IKIGAI::ECS {
class WaterComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = "WaterComponent";
		float Amplitude = .16f;
		float Wavelength = 7.f;
		float WindSpeed = 4.f;
		float Heading = 25.f;
		float Current = .12f;
		float Roughness = .14f;
		float Foam = .35f;
		float HullFoam = .85f;
		MATH::Vector3f Color = { .025f, .19f, .22f };
		template <class C> static void serde(C& c, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(c, value)
				.field(&Descriptor::Type, "WaterComponentType")
				.field(&Descriptor::Amplitude, "Amplitude", default_ { .16f })
				.field(&Descriptor::Wavelength, "Wavelength", default_ { 7.f })
				.field(&Descriptor::WindSpeed, "WindSpeed", default_ { 4.f })
				.field(&Descriptor::Heading, "Heading", default_ { 25.f })
				.field(&Descriptor::Current, "Current", default_ { .12f })
				.field(&Descriptor::Roughness, "Roughness", default_ { .14f })
				.field(&Descriptor::Foam, "Foam", default_ { .35f })
				.field(&Descriptor::HullFoam, "HullFoam", default_ { .85f })
				.field(&Descriptor::Color, "Color", default_ { MATH::Vector3f { .025f, .19f, .22f } });
		}
	};
	explicit WaterComponent(UTILS::Ref<Object> object);
	WaterComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const {
		return mSettings;
	}
	void configure(const Descriptor& settings);
	void advance(float dt);
	// Horizontal footprint and water-relative motion of the closest visible floating hull.
	struct FoamHull {
		MATH::Vector4f footprint = {};
		MATH::Vector4f motion = {};
		float agitation = 0;
	};
	void present(const FoamHull& hull);
	float displacement(float x, float z) const;
	MATH::Vector3f velocity(float x, float z) const;
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	Descriptor mSettings = {};
	// xy is the horizontal wave vector, z the amplitude, w the running phase.
	std::array<MATH::Vector4f, 3> mWaves = {};
	std::array<float, 3> mFrequency = {};
	MATH::Vector4f mPresentedPhases = {};
	MATH::Vector3f mFlow = {};
	std::shared_ptr<RENDER::MaterialInterface> mMaterial = {};
};
template <> inline std::string GetType<WaterComponent>() {
	return "class IKIGAI::ECS::WaterComponent";
}
template <> inline std::string GetComponentName<WaterComponent>() {
	return "WaterComponent";
}
}
