#pragma once
#if USE_PHYSICS_3D
#include "component.h"
#include "mathModule/math.h"
#include "physics3dLease.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class Physics3DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Physics3DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int BodyMode = 1;
		int ShapeKind = 1;
		float Radius = 0.5f;
		MATH::Vector3f HalfSize = MATH::Vector3f(0.5f);
		float Mass = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.1f;
		float GravityScale = 1.0f;
		MATH::Vector3f Velocity = MATH::Vector3f(0);
		MATH::Vector3f AngularVelocity = MATH::Vector3f(0);
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		bool AllowSleep = true;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Physics3DComponentType")
				.field(&Descriptor::BodyMode, "BodyMode", serde::attribute::default_ { 1 })
				.field(&Descriptor::ShapeKind, "ShapeKind", serde::attribute::default_ { 1 })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::HalfSize, "HalfSize", serde::attribute::default_ { MATH::Vector3f(0.5f) })
				.field(&Descriptor::Mass, "Mass", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Friction, "Friction", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::Restitution, "Restitution", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::GravityScale, "GravityScale", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Velocity, "Velocity", serde::attribute::default_ { MATH::Vector3f(0) })
				.field(&Descriptor::AngularVelocity, "AngularVelocity", serde::attribute::default_ { MATH::Vector3f(0) })
				.field(&Descriptor::LinearDamping, "LinearDamping", serde::attribute::default_ { 0.05f })
				.field(&Descriptor::AngularDamping, "AngularDamping", serde::attribute::default_ { 0.05f })
				.field(&Descriptor::AllowSleep, "AllowSleep", serde::attribute::default_ { true });
		}
	};
	explicit Physics3DComponent(UTILS::Ref<Object> obj);
	Physics3DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Physics3DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Physics3DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const {
		return settings;
	}
	Descriptor settings;
	std::shared_ptr<Physics3DLease> runtime;
	PHYSICS::Vec3 pendingForce, pendingImpulse;
	PHYSICS::Body* body() const {
		auto w = runtime ? runtime->world.lock() : nullptr;
		return w ? w->body(runtime->id) : nullptr;
	}
	void applyForce(PHYSICS::Vec3 f) {
		if (!PHYSICS_COMMON::finite(f))
			throw std::invalid_argument("Invalid force");
		pendingForce += f;
	}
	void applyImpulse(PHYSICS::Vec3 j) {
		if (!PHYSICS_COMMON::finite(j))
			throw std::invalid_argument("Invalid impulse");
		pendingImpulse += j;
	}
	std::uint64_t simulationId() const {
		return runtime ? runtime->id : 0;
	}
	void onDisable() override {
		runtime.reset();
	}
	void onDestroy() override {
		runtime.reset();
	}
	int getBodyMode() const {
		return settings.BodyMode;
	}
	void setBodyMode(int value) {
		settings.BodyMode = value;
	}
	IKI_PROPERTY(
		Name = BodyMode, Type = int, Getter = getBodyMode, Setter = setBodyMode, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int getShapeKind() const {
		return settings.ShapeKind;
	}
	void setShapeKind(int value) {
		settings.ShapeKind = value;
	}
	IKI_PROPERTY(Name = ShapeKind, Type = int, Getter = getShapeKind, Setter = setShapeKind, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_INT)
	float getRadius() const {
		return settings.Radius;
	}
	void setRadius(float value) {
		settings.Radius = value;
	}
	IKI_PROPERTY(
		Name = Radius, Type = float, Getter = getRadius, Setter = setRadius, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	MATH::Vector3f getHalfSize() const {
		return settings.HalfSize;
	}
	void setHalfSize(MATH::Vector3f value) {
		settings.HalfSize = value;
	}
	IKI_PROPERTY(Name = HalfSize, Type = MATH::Vector3f, Getter = getHalfSize, Setter = setHalfSize, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT_3)
	float getMass() const {
		return settings.Mass;
	}
	void setMass(float value) {
		settings.Mass = value;
	}
	IKI_PROPERTY(Name = Mass, Type = float, Getter = getMass, Setter = setMass, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getFriction() const {
		return settings.Friction;
	}
	void setFriction(float value) {
		settings.Friction = value;
	}
	IKI_PROPERTY(Name = Friction, Type = float, Getter = getFriction, Setter = setFriction, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getRestitution() const {
		return settings.Restitution;
	}
	void setRestitution(float value) {
		settings.Restitution = value;
	}
	IKI_PROPERTY(Name = Restitution, Type = float, Getter = getRestitution, Setter = setRestitution, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getGravityScale() const {
		return settings.GravityScale;
	}
	void setGravityScale(float value) {
		settings.GravityScale = value;
	}
	IKI_PROPERTY(Name = GravityScale, Type = float, Getter = getGravityScale, Setter = setGravityScale, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	MATH::Vector3f getVelocity() const {
		return settings.Velocity;
	}
	void setVelocity(MATH::Vector3f value) {
		settings.Velocity = value;
	}
	IKI_PROPERTY(Name = Velocity, Type = MATH::Vector3f, Getter = getVelocity, Setter = setVelocity, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT_3)
	MATH::Vector3f getAngularVelocity() const {
		return settings.AngularVelocity;
	}
	void setAngularVelocity(MATH::Vector3f value) {
		settings.AngularVelocity = value;
	}
	IKI_PROPERTY(Name = AngularVelocity, Type = MATH::Vector3f, Getter = getAngularVelocity, Setter = setAngularVelocity,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
	float getLinearDamping() const {
		return settings.LinearDamping;
	}
	void setLinearDamping(float value) {
		settings.LinearDamping = value;
	}
	IKI_PROPERTY(Name = LinearDamping, Type = float, Getter = getLinearDamping, Setter = setLinearDamping,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getAngularDamping() const {
		return settings.AngularDamping;
	}
	void setAngularDamping(float value) {
		settings.AngularDamping = value;
	}
	IKI_PROPERTY(Name = AngularDamping, Type = float, Getter = getAngularDamping, Setter = setAngularDamping,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	bool getAllowSleep() const {
		return settings.AllowSleep;
	}
	void setAllowSleep(bool value) {
		settings.AllowSleep = value;
	}
	IKI_PROPERTY(Name = AllowSleep, Type = bool, Getter = getAllowSleep, Setter = setAllowSleep, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = BOOL)
	static auto GetMembers() {
		return std::tuple { };
	}
};
IKI_CLASS(Groups = [Component])
class SoftBody3DComponent : public ComponentBase {
	IKI_GENERATED_BODY(SoftBody3DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Width = 3;
		int Height = 3;
		int Depth = 3;
		float Spacing = 0.2f;
		float Radius = 0.04f;
		float ParticleMass = 0.1f;
		float Compliance = 0.0f;
		float VolumeCompliance = 0.0f;
		float Damping = 0.5f;
		bool PinTop = false;
		bool SelfCollision = false;
		std::vector<MATH::Vector3f> Vertices = { };
		std::vector<int> Tetrahedra = { };
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "SoftBody3DComponentType")
				.field(&Descriptor::Width, "Width", serde::attribute::default_ { 3 })
				.field(&Descriptor::Height, "Height", serde::attribute::default_ { 3 })
				.field(&Descriptor::Depth, "Depth", serde::attribute::default_ { 3 })
				.field(&Descriptor::Spacing, "Spacing", serde::attribute::default_ { 0.2f })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.04f })
				.field(&Descriptor::ParticleMass, "ParticleMass", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::Compliance, "Compliance", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::VolumeCompliance, "VolumeCompliance", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::Damping, "Damping", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::PinTop, "PinTop", serde::attribute::default_ { false })
				.field(&Descriptor::SelfCollision, "SelfCollision", serde::attribute::default_ { false })
				.field(&Descriptor::Vertices, "Vertices", serde::attribute::default_ { std::vector<MATH::Vector3f> { } })
				.field(&Descriptor::Tetrahedra, "Tetrahedra", serde::attribute::default_ { std::vector<int> { } });
		}
	};
	explicit SoftBody3DComponent(UTILS::Ref<Object> obj);
	SoftBody3DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	SoftBody3DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: SoftBody3DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const {
		return settings;
	}
	Descriptor settings;
	std::shared_ptr<Physics3DLease> runtime;
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
	int getDepth() const {
		return settings.Depth;
	}
	void setDepth(int value) {
		settings.Depth = value;
	}
	IKI_PROPERTY(Name = Depth, Type = int, Getter = getDepth, Setter = setDepth, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
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
	float getCompliance() const {
		return settings.Compliance;
	}
	void setCompliance(float value) {
		settings.Compliance = value;
	}
	IKI_PROPERTY(Name = Compliance, Type = float, Getter = getCompliance, Setter = setCompliance, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getVolumeCompliance() const {
		return settings.VolumeCompliance;
	}
	void setVolumeCompliance(float value) {
		settings.VolumeCompliance = value;
	}
	IKI_PROPERTY(Name = VolumeCompliance, Type = float, Getter = getVolumeCompliance, Setter = setVolumeCompliance,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getDamping() const {
		return settings.Damping;
	}
	void setDamping(float value) {
		settings.Damping = value;
	}
	IKI_PROPERTY(
		Name = Damping, Type = float, Getter = getDamping, Setter = setDamping, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
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
IKI_CLASS(Groups = [Component])
class Cloth3DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Cloth3DComponent)
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
		float Damping = 0.5f;
		bool PinTop = true;
		bool SelfCollision = true;
		bool Impermeable = true;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Cloth3DComponentType")
				.field(&Descriptor::Width, "Width", serde::attribute::default_ { 12 })
				.field(&Descriptor::Height, "Height", serde::attribute::default_ { 12 })
				.field(&Descriptor::Spacing, "Spacing", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.02f })
				.field(&Descriptor::ParticleMass, "ParticleMass", serde::attribute::default_ { 0.01f })
				.field(&Descriptor::StretchCompliance, "StretchCompliance", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::ShearCompliance, "ShearCompliance", serde::attribute::default_ { 0.00001f })
				.field(&Descriptor::BendCompliance, "BendCompliance", serde::attribute::default_ { 0.001f })
				.field(&Descriptor::Damping, "Damping", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::PinTop, "PinTop", serde::attribute::default_ { true })
				.field(&Descriptor::SelfCollision, "SelfCollision", serde::attribute::default_ { true })
				.field(&Descriptor::Impermeable, "Impermeable", serde::attribute::default_ { true });
		}
	};
	explicit Cloth3DComponent(UTILS::Ref<Object> obj);
	Cloth3DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Cloth3DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Cloth3DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const {
		return settings;
	}
	Descriptor settings;
	std::shared_ptr<Physics3DLease> runtime;
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
	float getDamping() const {
		return settings.Damping;
	}
	void setDamping(float value) {
		settings.Damping = value;
	}
	IKI_PROPERTY(
		Name = Damping, Type = float, Getter = getDamping, Setter = setDamping, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
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
	bool getImpermeable() const {
		return settings.Impermeable;
	}
	void setImpermeable(bool value) {
		settings.Impermeable = value;
	}
	IKI_PROPERTY(Name = Impermeable, Type = bool, Getter = getImpermeable, Setter = setImpermeable, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = BOOL)
	static auto GetMembers() {
		return std::tuple { };
	}
};
IKI_CLASS(Groups = [Component])
class Fluid3DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Fluid3DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Width = 6;
		int Height = 6;
		int Depth = 6;
		float Spacing = 0.08f;
		float Radius = 0.04f;
		float ParticleMass = 0.512f;
		float SmoothingRadius = 0.16f;
		float RestDensity = 1000.0f;
		float Viscosity = 0.05f;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Fluid3DComponentType")
				.field(&Descriptor::Width, "Width", serde::attribute::default_ { 6 })
				.field(&Descriptor::Height, "Height", serde::attribute::default_ { 6 })
				.field(&Descriptor::Depth, "Depth", serde::attribute::default_ { 6 })
				.field(&Descriptor::Spacing, "Spacing", serde::attribute::default_ { 0.08f })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.04f })
				.field(&Descriptor::ParticleMass, "ParticleMass", serde::attribute::default_ { 0.512f })
				.field(&Descriptor::SmoothingRadius, "SmoothingRadius", serde::attribute::default_ { 0.16f })
				.field(&Descriptor::RestDensity, "RestDensity", serde::attribute::default_ { 1000.0f })
				.field(&Descriptor::Viscosity, "Viscosity", serde::attribute::default_ { 0.05f });
		}
	};
	explicit Fluid3DComponent(UTILS::Ref<Object> obj);
	Fluid3DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Fluid3DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Fluid3DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const {
		return settings;
	}
	Descriptor settings;
	std::shared_ptr<Physics3DLease> runtime;
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
	int getDepth() const {
		return settings.Depth;
	}
	void setDepth(int value) {
		settings.Depth = value;
	}
	IKI_PROPERTY(Name = Depth, Type = int, Getter = getDepth, Setter = setDepth, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
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
	float getSmoothingRadius() const {
		return settings.SmoothingRadius;
	}
	void setSmoothingRadius(float value) {
		settings.SmoothingRadius = value;
	}
	IKI_PROPERTY(Name = SmoothingRadius, Type = float, Getter = getSmoothingRadius, Setter = setSmoothingRadius,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getRestDensity() const {
		return settings.RestDensity;
	}
	void setRestDensity(float value) {
		settings.RestDensity = value;
	}
	IKI_PROPERTY(Name = RestDensity, Type = float, Getter = getRestDensity, Setter = setRestDensity, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getViscosity() const {
		return settings.Viscosity;
	}
	void setViscosity(float value) {
		settings.Viscosity = value;
	}
	IKI_PROPERTY(Name = Viscosity, Type = float, Getter = getViscosity, Setter = setViscosity, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	static auto GetMembers() {
		return std::tuple { };
	}
};
IKI_CLASS(Groups = [Component])
class ForceField3DComponent : public ComponentBase {
	IKI_GENERATED_BODY(ForceField3DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Kind = 3;
		MATH::Vector3f Vector = MATH::Vector3f(3, 0, 0);
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
				.field(&Descriptor::Type, "ForceField3DComponentType")
				.field(&Descriptor::Kind, "Kind", serde::attribute::default_ { 3 })
				.field(&Descriptor::Vector, "Vector", serde::attribute::default_ { MATH::Vector3f(3, 0, 0) })
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
	explicit ForceField3DComponent(UTILS::Ref<Object> obj);
	ForceField3DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	ForceField3DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: ForceField3DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const {
		return settings;
	}
	Descriptor settings;
	std::shared_ptr<Physics3DLease> runtime;
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
	MATH::Vector3f getVector() const {
		return settings.Vector;
	}
	void setVector(MATH::Vector3f value) {
		settings.Vector = value;
	}
	IKI_PROPERTY(Name = Vector, Type = MATH::Vector3f, Getter = getVector, Setter = setVector, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
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
template <> inline std::string GetType<Physics3DComponent>() {
	return "class IKIGAI::ECS::Physics3DComponent";
}
template <> inline std::string GetComponentName<Physics3DComponent>() {
	return "Physics3DComponent";
}
template <> inline std::string GetType<SoftBody3DComponent>() {
	return "class IKIGAI::ECS::SoftBody3DComponent";
}
template <> inline std::string GetComponentName<SoftBody3DComponent>() {
	return "SoftBody3DComponent";
}
template <> inline std::string GetType<Cloth3DComponent>() {
	return "class IKIGAI::ECS::Cloth3DComponent";
}
template <> inline std::string GetComponentName<Cloth3DComponent>() {
	return "Cloth3DComponent";
}
template <> inline std::string GetType<Fluid3DComponent>() {
	return "class IKIGAI::ECS::Fluid3DComponent";
}
template <> inline std::string GetComponentName<Fluid3DComponent>() {
	return "Fluid3DComponent";
}
template <> inline std::string GetType<ForceField3DComponent>() {
	return "class IKIGAI::ECS::ForceField3DComponent";
}
template <> inline std::string GetComponentName<ForceField3DComponent>() {
	return "ForceField3DComponent";
}
}
#include "generated/physics3dComponent.generated.h"

#endif // USE_PHYSICS_3D
