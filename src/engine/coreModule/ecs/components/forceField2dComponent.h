#pragma once
#if USE_PHYSICS_2D
#include "component.h"
#include "mathModule/math.h"
#include "physics2dComponent.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class ForceField2DComponent : public ComponentBase {
	IKI_GENERATED_BODY(ForceField2DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Kind = 3;
		MATH::Vector2f Vector = MATH::Vector2f(3, 0);
		float Radius = 0.0f;
		float Linear = 0.0f;
		float Quadratic = 1.0f;
		float Area = 1.0f;
		float Density = 1.225f;
		float WaterHeight = 0.0f;
		float MaxDepth = 0.5f;
		float Volume = 1.0f;
		float GustAmplitude = 0.0f;
		float GustFrequency = 1.0f;
		int Seed = 0;
		int Targets = 15;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "ForceField2DComponentType")
				.field(&Descriptor::Kind, "Kind", serde::attribute::default_ { 3 })
				.field(&Descriptor::Vector, "Vector", serde::attribute::default_ { MATH::Vector2f(3, 0) })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::Linear, "Linear", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::Quadratic, "Quadratic", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Area, "Area", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Density, "Density", serde::attribute::default_ { 1.225f })
				.field(&Descriptor::WaterHeight, "WaterHeight", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::MaxDepth, "MaxDepth", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::Volume, "Volume", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::GustAmplitude, "GustAmplitude", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::GustFrequency, "GustFrequency", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Seed, "Seed", serde::attribute::default_ { 0 })
				.field(&Descriptor::Targets, "Targets", serde::attribute::default_ { 15 });
		}
	};
	explicit ForceField2DComponent(UTILS::Ref<Object> obj);
	ForceField2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	ForceField2DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: ForceField2DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
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
	int getKind() const {
		return settings.Kind;
	}
	void setKind(int value) {
		settings.Kind = value;
	}
	IKI_PROPERTY(Name = Kind, Type = int, Getter = getKind, Setter = setKind, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	MATH::Vector2f getVector() const {
		return settings.Vector;
	}
	void setVector(MATH::Vector2f value) {
		settings.Vector = value;
	}
	IKI_PROPERTY(Name = Vector, Type = MATH::Vector2f, Getter = getVector, Setter = setVector, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT_3)
	float getRadius() const {
		return settings.Radius;
	}
	void setRadius(float value) {
		settings.Radius = value;
	}
	IKI_PROPERTY(
		Name = Radius, Type = float, Getter = getRadius, Setter = setRadius, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getLinear() const {
		return settings.Linear;
	}
	void setLinear(float value) {
		settings.Linear = value;
	}
	IKI_PROPERTY(
		Name = Linear, Type = float, Getter = getLinear, Setter = setLinear, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getQuadratic() const {
		return settings.Quadratic;
	}
	void setQuadratic(float value) {
		settings.Quadratic = value;
	}
	IKI_PROPERTY(Name = Quadratic, Type = float, Getter = getQuadratic, Setter = setQuadratic, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getArea() const {
		return settings.Area;
	}
	void setArea(float value) {
		settings.Area = value;
	}
	IKI_PROPERTY(Name = Area, Type = float, Getter = getArea, Setter = setArea, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getDensity() const {
		return settings.Density;
	}
	void setDensity(float value) {
		settings.Density = value;
	}
	IKI_PROPERTY(
		Name = Density, Type = float, Getter = getDensity, Setter = setDensity, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getWaterHeight() const {
		return settings.WaterHeight;
	}
	void setWaterHeight(float value) {
		settings.WaterHeight = value;
	}
	IKI_PROPERTY(Name = WaterHeight, Type = float, Getter = getWaterHeight, Setter = setWaterHeight, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getMaxDepth() const {
		return settings.MaxDepth;
	}
	void setMaxDepth(float value) {
		settings.MaxDepth = value;
	}
	IKI_PROPERTY(Name = MaxDepth, Type = float, Getter = getMaxDepth, Setter = setMaxDepth, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getVolume() const {
		return settings.Volume;
	}
	void setVolume(float value) {
		settings.Volume = value;
	}
	IKI_PROPERTY(
		Name = Volume, Type = float, Getter = getVolume, Setter = setVolume, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getGustAmplitude() const {
		return settings.GustAmplitude;
	}
	void setGustAmplitude(float value) {
		settings.GustAmplitude = value;
	}
	IKI_PROPERTY(Name = GustAmplitude, Type = float, Getter = getGustAmplitude, Setter = setGustAmplitude,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getGustFrequency() const {
		return settings.GustFrequency;
	}
	void setGustFrequency(float value) {
		settings.GustFrequency = value;
	}
	IKI_PROPERTY(Name = GustFrequency, Type = float, Getter = getGustFrequency, Setter = setGustFrequency,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	int getSeed() const {
		return settings.Seed;
	}
	void setSeed(int value) {
		settings.Seed = value;
	}
	IKI_PROPERTY(Name = Seed, Type = int, Getter = getSeed, Setter = setSeed, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int getTargets() const {
		return settings.Targets;
	}
	void setTargets(int value) {
		settings.Targets = value;
	}
	IKI_PROPERTY(
		Name = Targets, Type = int, Getter = getTargets, Setter = setTargets, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	static auto GetMembers() {
		return std::tuple { };
	}
};
template <> inline std::string GetType<ForceField2DComponent>() {
	return "class IKIGAI::ECS::ForceField2DComponent";
}
template <> inline std::string GetComponentName<ForceField2DComponent>() {
	return "ForceField2DComponent";
}
}
#include "generated/forceField2dComponent.generated.h"
#endif
