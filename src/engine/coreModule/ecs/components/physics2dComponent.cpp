#include "physics2dComponent.h"
#if USE_PHYSICS_2D
#include "coreModule/ecs/object.h"
using namespace IKIGAI;
using namespace IKIGAI::ECS;
Physics2DLease::~Physics2DLease() {
	if (!id)
		return;
	if (auto simulation = world.lock())
		switch (kind) {
		case Kind::Field:
			simulation->forces.remove(id);
			break;
		case Kind::Body:
			simulation->destroyBody(id);
			break;
		case Kind::Joint:
			simulation->destroyJoint(id);
			break;
		case Kind::Soft:
			simulation->destroySoftBody(id);
			break;
		case Kind::Fluid:
			simulation->destroyFluid(id);
			break;
		}
}
Physics2DComponent::Physics2DComponent(UTILS::Ref<Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "Physics2DComponent";
	settings.Type = GetType<Physics2DComponent>();
}
Physics2DComponent::Physics2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor)
	: Physics2DComponent(obj) {
	settings = descriptor;
	settings.Type = GetType<Physics2DComponent>();
}
Physics2DComponent::Descriptor Physics2DComponent::getDescriptor() const {
	return settings;
}
SoftBody2DComponent::SoftBody2DComponent(UTILS::Ref<Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "SoftBody2DComponent";
	settings.Type = GetType<SoftBody2DComponent>();
}
SoftBody2DComponent::SoftBody2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor)
	: SoftBody2DComponent(obj) {
	settings = descriptor;
	settings.Type = GetType<SoftBody2DComponent>();
}
SoftBody2DComponent::Descriptor SoftBody2DComponent::getDescriptor() const {
	return settings;
}
Fluid2DComponent::Fluid2DComponent(UTILS::Ref<Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "Fluid2DComponent";
	settings.Type = GetType<Fluid2DComponent>();
}
Fluid2DComponent::Fluid2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor)
	: Fluid2DComponent(obj) {
	settings = descriptor;
	settings.Type = GetType<Fluid2DComponent>();
}
Fluid2DComponent::Descriptor Fluid2DComponent::getDescriptor() const {
	return settings;
}
Joint2DComponent::Joint2DComponent(UTILS::Ref<Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "Joint2DComponent";
	settings.Type = GetType<Joint2DComponent>();
}
Joint2DComponent::Joint2DComponent(UTILS::Ref<Object> obj, const Descriptor& descriptor)
	: Joint2DComponent(obj) {
	settings = descriptor;
	settings.Type = GetType<Joint2DComponent>();
}
Joint2DComponent::Descriptor Joint2DComponent::getDescriptor() const {
	return settings;
}
PHYSICS2D::Body* Physics2DComponent::body() const {
	if (runtime)
		if (auto w = runtime->world.lock())
			return w->body(runtime->id);
	return nullptr;
}
void Physics2DComponent::applyForce(MATH::Vector2f f) {
	if (runtime)
		if (auto w = runtime->world.lock())
			if (auto b = w->body(runtime->id)) {
				w->applyForce(b->id, { f.x, f.y }, b->position);
				return;
			}
	pendingForce += PHYSICS2D::Vec2 { f.x, f.y };
}
void Physics2DComponent::applyImpulse(MATH::Vector2f p) {
	if (runtime)
		if (auto w = runtime->world.lock())
			if (auto b = w->body(runtime->id)) {
				w->applyImpulse(b->id, { p.x, p.y }, b->position);
				return;
			}
	pendingImpulse += PHYSICS2D::Vec2 { p.x, p.y };
}
void Physics2DComponent::teleport(MATH::Vector2f p, float angle) {
	obj->mTransform->setLocalPosition({ p.x, p.y, obj->mTransform->getLocalPosition().z });
	obj->mTransform->setLocalRotation(MATH::QuaternionF(0, 0, std::sin(angle / 2), std::cos(angle / 2)));
	if (runtime)
		if (auto w = runtime->world.lock())
			w->teleport(runtime->id, { p.x, p.y }, angle);
}
void Physics2DComponent::setVelocity(MATH::Vector2f v, float angular) {
	if (runtime)
		if (auto w = runtime->world.lock()) {
			w->setVelocity(runtime->id, { v.x, v.y }, angular);
			return;
		}
	settings.Velocity = v;
	settings.AngularVelocity = angular;
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Physics2DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SoftBody2DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Fluid2DComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::Joint2DComponent>();
	return true;
}();
}

#endif
