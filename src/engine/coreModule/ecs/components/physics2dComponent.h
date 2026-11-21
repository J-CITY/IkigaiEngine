#pragma once
#if USE_PHYSICS_2D
#include "component.h"
#include "mathModule/math.h"
#include "physics2dModule/physics2d.h"
#include "reflectionModule/reflection_macros.h"

namespace IKIGAI::ECS {
// The component owns a shared lease, not a pointer into an ECS array. Sparse
// storage can move components; the last lease releases the simulation object.
// weak_ptr<World> makes destruction safe even after its scene has gone away.
struct Physics2DLease {
	enum class Kind { Body, Soft, Fluid, Joint, Field };
	std::weak_ptr<PHYSICS2D::World> world;
	PHYSICS2D::Id id = 0;
	Kind kind = Kind::Body;
	std::string signature;
	~Physics2DLease();
};
IKI_CLASS(Groups = [Component])
class Physics2DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Physics2DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int BodyMode = 1;
		int ShapeKind = 1;
		float Radius = 0.5f;
		MATH::Vector2f HalfSize = MATH::Vector2f(0.5f, 0.5f);
		std::vector<MATH::Vector2f> Vertices = { };
		float Mass = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.1f;
		float GravityScale = 1.0f;
		MATH::Vector2f Velocity = MATH::Vector2f(0, 0);
		float AngularVelocity = 0.0f;
		bool AllowSleep = true;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Physics2DComponentType")
				.field(&Descriptor::BodyMode, "BodyMode", serde::attribute::default_ { 1 })
				.field(&Descriptor::ShapeKind, "ShapeKind", serde::attribute::default_ { 1 })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::HalfSize, "HalfSize", serde::attribute::default_ { MATH::Vector2f(0.5f, 0.5f) })
				.field(&Descriptor::Vertices, "Vertices", serde::attribute::default_ { std::vector<MATH::Vector2f> { } })
				.field(&Descriptor::Mass, "Mass", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Friction, "Friction", serde::attribute::default_ { 0.5f })
				.field(&Descriptor::Restitution, "Restitution", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::GravityScale, "GravityScale", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Velocity, "Velocity", serde::attribute::default_ { MATH::Vector2f(0, 0) })
				.field(&Descriptor::AngularVelocity, "AngularVelocity", serde::attribute::default_ { 0.0f })
				.field(&Descriptor::AllowSleep, "AllowSleep", serde::attribute::default_ { true });
		}
	};
	explicit Physics2DComponent(UTILS::Ref<Object> obj);
	MATH::Vector2f getHalfSize() const {
		return settings.HalfSize;
	}
	void setHalfSize(MATH::Vector2f value) {
		settings.HalfSize = value;
	}
	IKI_PROPERTY(Name = HalfSize, Type = MATH::Vector2f, Getter = getHalfSize, Setter = setHalfSize, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f getVelocity() const {
		return settings.Velocity;
	}
	void setInitialVelocity(MATH::Vector2f value) {
		settings.Velocity = value;
	}
	IKI_PROPERTY(Name = Velocity, Type = MATH::Vector2f, Getter = getVelocity, Setter = setInitialVelocity,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT_3)
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
	float getAngularVelocity() const {
		return settings.AngularVelocity;
	}
	void setAngularVelocity(float value) {
		settings.AngularVelocity = value;
	}
	IKI_PROPERTY(Name = AngularVelocity, Type = float, Getter = getAngularVelocity, Setter = setAngularVelocity,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	bool getAllowSleep() const {
		return settings.AllowSleep;
	}
	void setAllowSleep(bool value) {
		settings.AllowSleep = value;
	}
	IKI_PROPERTY(Name = AllowSleep, Type = bool, Getter = getAllowSleep, Setter = setAllowSleep, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = BOOL)
	Physics2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Physics2DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Physics2DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	Descriptor settings;
	std::shared_ptr<Physics2DLease> runtime;
	PHYSICS2D::Id simulationId() const {
		return runtime ? runtime->id : 0;
	}
	static auto GetMembers() {
		return std::tuple { };
	}
	PHYSICS2D::Body* body() const;
	// OnStart/first fixed callback can run before the scene creates its body.
	// Keep these commands until the adapter has a valid simulation handle.
	PHYSICS2D::Vec2 pendingForce, pendingImpulse;
	void applyForce(MATH::Vector2f force);
	void applyImpulse(MATH::Vector2f impulse);
	void teleport(MATH::Vector2f position, float radians);
	void setVelocity(MATH::Vector2f velocity, float radiansPerSecond = 0);
};
IKI_CLASS(Groups = [Component])
class SoftBody2DComponent : public ComponentBase {
	IKI_GENERATED_BODY(SoftBody2DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Width = 4;
		int Height = 4;
		float Spacing = 0.2f;
		float Radius = 0.06f;
		float ParticleMass = 0.1f;
		float Stiffness = 0.95f;
		float AreaStiffness = 0.8f;
		bool PinTop = false;
		bool PreserveArea = true;
		bool SelfCollision = false;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "SoftBody2DComponentType")
				.field(&Descriptor::Width, "Width", serde::attribute::default_ { 4 })
				.field(&Descriptor::Height, "Height", serde::attribute::default_ { 4 })
				.field(&Descriptor::Spacing, "Spacing", serde::attribute::default_ { 0.2f })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.06f })
				.field(&Descriptor::ParticleMass, "ParticleMass", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::Stiffness, "Stiffness", serde::attribute::default_ { 0.95f })
				.field(&Descriptor::AreaStiffness, "AreaStiffness", serde::attribute::default_ { 0.8f })
				.field(&Descriptor::PinTop, "PinTop", serde::attribute::default_ { false })
				.field(&Descriptor::PreserveArea, "PreserveArea", serde::attribute::default_ { true })
				.field(&Descriptor::SelfCollision, "SelfCollision", serde::attribute::default_ { false });
		}
	};
	explicit SoftBody2DComponent(UTILS::Ref<Object> obj);
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
	float getStiffness() const {
		return settings.Stiffness;
	}
	void setStiffness(float value) {
		settings.Stiffness = value;
	}
	IKI_PROPERTY(Name = Stiffness, Type = float, Getter = getStiffness, Setter = setStiffness, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	float getAreaStiffness() const {
		return settings.AreaStiffness;
	}
	void setAreaStiffness(float value) {
		settings.AreaStiffness = value;
	}
	IKI_PROPERTY(Name = AreaStiffness, Type = float, Getter = getAreaStiffness, Setter = setAreaStiffness,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	bool getPinTop() const {
		return settings.PinTop;
	}
	void setPinTop(bool value) {
		settings.PinTop = value;
	}
	IKI_PROPERTY(Name = PinTop, Type = bool, Getter = getPinTop, Setter = setPinTop, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	bool getPreserveArea() const {
		return settings.PreserveArea;
	}
	void setPreserveArea(bool value) {
		settings.PreserveArea = value;
	}
	IKI_PROPERTY(Name = PreserveArea, Type = bool, Getter = getPreserveArea, Setter = setPreserveArea, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = BOOL)
	bool getSelfCollision() const {
		return settings.SelfCollision;
	}
	void setSelfCollision(bool value) {
		settings.SelfCollision = value;
	}
	IKI_PROPERTY(Name = SelfCollision, Type = bool, Getter = getSelfCollision, Setter = setSelfCollision, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = BOOL)
	SoftBody2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	SoftBody2DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: SoftBody2DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	Descriptor settings;
	std::shared_ptr<Physics2DLease> runtime;
	PHYSICS2D::Id simulationId() const {
		return runtime ? runtime->id : 0;
	}
	static auto GetMembers() {
		return std::tuple { };
	}
};
IKI_CLASS(Groups = [Component])
class Fluid2DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Fluid2DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int Width = 12;
		int Height = 8;
		float Spacing = 0.1f;
		float Radius = 0.04f;
		float SmoothingRadius = 0.25f;
		float ParticleMass = 0.01f;
		float RestDensity = 1.0f;
		float Viscosity = 0.1f;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Fluid2DComponentType")
				.field(&Descriptor::Width, "Width", serde::attribute::default_ { 12 })
				.field(&Descriptor::Height, "Height", serde::attribute::default_ { 8 })
				.field(&Descriptor::Spacing, "Spacing", serde::attribute::default_ { 0.1f })
				.field(&Descriptor::Radius, "Radius", serde::attribute::default_ { 0.04f })
				.field(&Descriptor::SmoothingRadius, "SmoothingRadius", serde::attribute::default_ { 0.25f })
				.field(&Descriptor::ParticleMass, "ParticleMass", serde::attribute::default_ { 0.01f })
				.field(&Descriptor::RestDensity, "RestDensity", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::Viscosity, "Viscosity", serde::attribute::default_ { 0.1f });
		}
	};
	explicit Fluid2DComponent(UTILS::Ref<Object> obj);
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
	float getSmoothingRadius() const {
		return settings.SmoothingRadius;
	}
	void setSmoothingRadius(float value) {
		settings.SmoothingRadius = value;
	}
	IKI_PROPERTY(Name = SmoothingRadius, Type = float, Getter = getSmoothingRadius, Setter = setSmoothingRadius,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_FLOAT)
	float getParticleMass() const {
		return settings.ParticleMass;
	}
	void setParticleMass(float value) {
		settings.ParticleMass = value;
	}
	IKI_PROPERTY(Name = ParticleMass, Type = float, Getter = getParticleMass, Setter = setParticleMass, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
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
	Fluid2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Fluid2DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Fluid2DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	Descriptor settings;
	std::shared_ptr<Physics2DLease> runtime;
	PHYSICS2D::Id simulationId() const {
		return runtime ? runtime->id : 0;
	}
	static auto GetMembers() {
		return std::tuple { };
	}
};
IKI_CLASS(Groups = [Component])
class Joint2DComponent : public ComponentBase {
	IKI_GENERATED_BODY(Joint2DComponent)
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type;
		int PhysicsTargetId = -1;
		int JointKind = 0;
		MATH::Vector2f AnchorA = MATH::Vector2f(0, 0);
		MATH::Vector2f AnchorB = MATH::Vector2f(0, 0);
		float Distance = 1.0f;
		bool CollideConnected = false;
		template <class Context> static constexpr auto serde(Context& context, Descriptor& value) {
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "Joint2DComponentType")
				.field(&Descriptor::PhysicsTargetId, "PhysicsTargetId", serde::attribute::default_ { -1 })
				.field(&Descriptor::JointKind, "JointKind", serde::attribute::default_ { 0 })
				.field(&Descriptor::AnchorA, "AnchorA", serde::attribute::default_ { MATH::Vector2f(0, 0) })
				.field(&Descriptor::AnchorB, "AnchorB", serde::attribute::default_ { MATH::Vector2f(0, 0) })
				.field(&Descriptor::Distance, "Distance", serde::attribute::default_ { 1.0f })
				.field(&Descriptor::CollideConnected, "CollideConnected", serde::attribute::default_ { false });
		}
	};
	explicit Joint2DComponent(UTILS::Ref<Object> obj);
	MATH::Vector2f getAnchorA() const {
		return settings.AnchorA;
	}
	void setAnchorA(MATH::Vector2f value) {
		settings.AnchorA = value;
	}
	IKI_PROPERTY(Name = AnchorA, Type = MATH::Vector2f, Getter = getAnchorA, Setter = setAnchorA, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT_3)
	MATH::Vector2f getAnchorB() const {
		return settings.AnchorB;
	}
	void setAnchorB(MATH::Vector2f value) {
		settings.AnchorB = value;
	}
	IKI_PROPERTY(Name = AnchorB, Type = MATH::Vector2f, Getter = getAnchorB, Setter = setAnchorB, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT_3)
	int getPhysicsTargetId() const {
		return settings.PhysicsTargetId;
	}
	void setPhysicsTargetId(int value) {
		settings.PhysicsTargetId = value;
	}
	IKI_PROPERTY(Name = PhysicsTargetId, Type = int, Getter = getPhysicsTargetId, Setter = setPhysicsTargetId,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = DRAG_INT)
	int getJointKind() const {
		return settings.JointKind;
	}
	void setJointKind(int value) {
		settings.JointKind = value;
	}
	IKI_PROPERTY(Name = JointKind, Type = int, Getter = getJointKind, Setter = setJointKind, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_INT)
	float getDistance() const {
		return settings.Distance;
	}
	void setDistance(float value) {
		settings.Distance = value;
	}
	IKI_PROPERTY(Name = Distance, Type = float, Getter = getDistance, Setter = setDistance, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = DRAG_FLOAT)
	bool getCollideConnected() const {
		return settings.CollideConnected;
	}
	void setCollideConnected(bool value) {
		settings.CollideConnected = value;
	}
	IKI_PROPERTY(Name = CollideConnected, Type = bool, Getter = getCollideConnected, Setter = setCollideConnected,
		Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	Joint2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor);
	Joint2DComponent(UTILS::Ref<Object> obj, const ComponentBase::Descriptor& descriptor)
		: Joint2DComponent(obj, static_cast<const Descriptor&>(descriptor)) { }
	Descriptor getDescriptor() const;
	Descriptor settings;
	std::shared_ptr<Physics2DLease> runtime;
	PHYSICS2D::Id simulationId() const {
		return runtime ? runtime->id : 0;
	}
	static auto GetMembers() {
		return std::tuple { };
	}
};
template <> inline std::string GetType<Physics2DComponent>() {
	return "class IKIGAI::ECS::Physics2DComponent";
}
template <> inline std::string GetComponentName<Physics2DComponent>() {
	return "Physics2DComponent";
}
template <> inline std::string GetType<SoftBody2DComponent>() {
	return "class IKIGAI::ECS::SoftBody2DComponent";
}
template <> inline std::string GetComponentName<SoftBody2DComponent>() {
	return "SoftBody2DComponent";
}
template <> inline std::string GetType<Fluid2DComponent>() {
	return "class IKIGAI::ECS::Fluid2DComponent";
}
template <> inline std::string GetComponentName<Fluid2DComponent>() {
	return "Fluid2DComponent";
}
template <> inline std::string GetType<Joint2DComponent>() {
	return "class IKIGAI::ECS::Joint2DComponent";
}
template <> inline std::string GetComponentName<Joint2DComponent>() {
	return "Joint2DComponent";
}
}
#include "generated/physics2dComponent.generated.h"
#endif
