#pragma once
#if USE_PHYSICS_2D
#include "component.h"
#include "mathModule/math.h"
#include "physics2dComponent.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class Cloth2DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Cloth2DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Width = 12;
		int Height = 12;
		float Spacing = 0.1f;
		float Radius = 0.02f;
		float ParticleMass = 0.01f;
		float StretchCompliance = 0.0f;
		float ShearCompliance = 0.00001f;
		float BendCompliance = 0.001f;
		bool PinTop = true;
		bool SelfCollision = true;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Cloth2DComponentType")
				.field(&Descriptor::Width, "Width", serde::attribute::default_ { 12 })
				.field(&Descriptor::Height, "Height", serde::attribute::default_ { 12 })
				.field(&Descriptor::Spacing, "Spacing", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.02f })
				.field(&Descriptor::ParticleMass, "ParticleMass", serde::attribute::default_ { 0.01f })
				.field(&Descriptor::StretchCompliance, "StretchCompliance", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::ShearCompliance, "ShearCompliance", serde::attribute::default_ { 0.00001f })
				.field(&Descriptor::BendCompliance, "BendCompliance", serde::attribute::default_ { 0.001f })
				.field(&Descriptor::PinTop, "PinTop", serde::attribute::default_ { true })
				.field(&Descriptor::SelfCollision, "SelfCollision", serde::attribute::default_ { true });
		}
	};
	explicit Cloth2DComponent(UTILS::Ref<Object> obj);
	Cloth2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Cloth2DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Cloth2DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const {
		return settings;
	}
	Descriptor settings;
	std::shared_ptr<Physics2DLease> runtime;
	std::uint64_t simulationId() const {
		return runtime ? runtime->id : 0;
	}
	void onDisable() override {
		runtime.reset();
	}
	void onDestroy() override {
		runtime.reset();
	}
	int getWidth() const {
		return settings.Width;
	}
	void setWidth(int value) {
		settings.Width = value;
	}
	IKI_PROPERTY(Name = Width, Type = int, Getter = getWidth, Setter = setWidth, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int getHeight() const {
		return settings.Height;
	}
	void setHeight(int value) {
		settings.Height = value;
	}
	IKI_PROPERTY(
		Name = Height, Type = int, Getter = getHeight, Setter = setHeight, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	float getSpacing() const {
		return settings.Spacing;
	}
	void setSpacing(float value) {
		settings.Spacing = value;
	}
	IKI_PROPERTY(
		Name = Spacing, Type = float, Getter = getSpacing, Setter = setSpacing, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getRadius() const {
		return settings.Radius;
	}
	void setRadius(float value) {
		settings.Radius = value;
	}
	IKI_PROPERTY(
		Name = Radius, Type = float, Getter = getRadius, Setter = setRadius, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getParticleMass() const {
		return settings.ParticleMass;
	}
	void setParticleMass(float value) {
		settings.ParticleMass = value;
	}
	IKI_PROPERTY(Name = ParticleMass, Type = float, Getter = getParticleMass, Setter = setParticleMass, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getStretchCompliance() const {
		return settings.StretchCompliance;
	}
	void setStretchCompliance(float value) {
		settings.StretchCompliance = value;
	}
	IKI_PROPERTY(Name = StretchCompliance, Type = float, Getter = getStretchCompliance, Setter = setStretchCompliance,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getShearCompliance() const {
		return settings.ShearCompliance;
	}
	void setShearCompliance(float value) {
		settings.ShearCompliance = value;
	}
	IKI_PROPERTY(Name = ShearCompliance, Type = float, Getter = getShearCompliance, Setter = setShearCompliance,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getBendCompliance() const {
		return settings.BendCompliance;
	}
	void setBendCompliance(float value) {
		settings.BendCompliance = value;
	}
	IKI_PROPERTY(Name = BendCompliance, Type = float, Getter = getBendCompliance, Setter = setBendCompliance,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	bool getPinTop() const {
		return settings.PinTop;
	}
	void setPinTop(bool value) {
		settings.PinTop = value;
	}
	IKI_PROPERTY(Name = PinTop, Type = bool, Getter = getPinTop, Setter = setPinTop, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool getSelfCollision() const {
		return settings.SelfCollision;
	}
	void setSelfCollision(bool value) {
		settings.SelfCollision = value;
	}
	IKI_PROPERTY(Name = SelfCollision, Type = bool, Getter = getSelfCollision, Setter = setSelfCollision, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = BOOL)
	static auto GetMembers() {
		return std::tuple { };
	}
};
template <> inline std::string GetType<Cloth2DComponent>() {
	return "class IKIGAI::ECS::Cloth2DComponent";
}
template <> inline std::string GetComponentName<Cloth2DComponent>() {
	return "Cloth2DComponent";
}
}
#include "generated/cloth2dComponent.generated.h"
#endif
