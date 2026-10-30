#include "characterBehaviorHost.h"
#include "coreModule/ecs/componentReference.inl"
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/thirdPersonController.h"
#include <cmath>

namespace IKIGAI::AI {
CharacterBehaviorHost::CharacterBehaviorHost(std::weak_ptr<ECS::Object> owner)
	: mOwner(std::move(owner)) { }
CharacterBehaviorHost::~CharacterBehaviorHost() {
	clear();
}
void CharacterBehaviorHost::bind(SCENE_SYSTEM::Scene& scene, const ECS::ComponentReference& reference) {
	if (reference.empty()) {
		return;
	}
#if USE_PHYSICS_3D
	auto owner = mOwner.lock();
	auto motor = ECS::Resolve<ECS::CharacterMotorComponent>(scene, reference);
	if (!owner || !motor || motor->obj.getPtr() != owner.get()) {
		throw std::runtime_error("Behavior Tree must control its owning object's motor");
	}
	for (const auto& object : scene.getObjects()) {
		if (auto controller = object->getComponent<ECS::ThirdPersonController>(); controller && !controller->settings.Motor.empty()) {
			auto playerMotor = ECS::Resolve<ECS::CharacterMotorComponent>(scene, controller->settings.Motor);
			if (playerMotor && playerMotor.get() == motor.get()) {
				throw std::runtime_error("AI and player controller cannot share a motor");
			}
		}
	}
	mMotor = UTILS::WeakPtr<ECS::ComponentBase>(static_cast<ECS::ComponentBase*>(motor.get()));
#else
	(void)scene;
	throw std::runtime_error("Behavior Tree Motor requires USE_PHYSICS_3D");
#endif
}
std::optional<MATH::Vector3f> CharacterBehaviorHost::position(const EntityRef& entity) const {
	auto handle = entity.handle.lock();
	auto object = handle ? std::static_pointer_cast<ECS::Object>(handle) : nullptr;
	if (!object || !object->isAlive() || !object->getIsActive() || !object->mTransform) {
		return std::nullopt;
	}
#if USE_PHYSICS_3D
	if (auto motor = object->getComponent<ECS::CharacterMotorComponent>(); motor && motor->initialized) {
		return motor->motor.capsule.feet;
	}
#endif
	return object->mTransform->getWorldPosition();
}
std::optional<MATH::Vector3f> CharacterBehaviorHost::ownerPosition() const {
	auto owner = mOwner.lock();
	if (!owner) {
		return std::nullopt;
	}
	return position({ uint64_t(owner->getID().getUniqueId().value()), owner });
}
std::optional<float> CharacterBehaviorHost::ownerHeading() const {
#if USE_PHYSICS_3D
	if (mMotor) {
		return static_cast<const ECS::CharacterMotorComponent*>(mMotor.get())->heading;
	}
#endif
	return std::nullopt;
}
bool CharacterBehaviorHost::move(const TaskToken& token, MATH::Vector3f velocity, std::optional<float> heading) {
	if (!mMotor || !std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) || (heading && !std::isfinite(*heading))) {
		return false;
	}
	if (mControlling && !mToken.sameRun(token)) {
		return false;
	}
	mToken = token;
	mVelocity = velocity;
	mHeading = heading;
	mQueued = true;
	mControlling = true;
	return true;
}
void CharacterBehaviorHost::apply() {
#if USE_PHYSICS_3D
	if (mMotor) {
		auto& motor = *static_cast<ECS::CharacterMotorComponent*>(mMotor.get());
		motor.wish = mQueued ? mVelocity : MATH::Vector3f {};
		motor.sprint = false;
		motor.jump = false;
		if (mQueued && mHeading) {
			motor.desiredHeading = *mHeading;
		}
	}
#endif
	mQueued = false;
}
void CharacterBehaviorHost::clear() {
#if USE_PHYSICS_3D
	if (mControlling && mMotor) {
		auto& motor = *static_cast<ECS::CharacterMotorComponent*>(mMotor.get());
		motor.wish = {};
		motor.sprint = false;
		motor.jump = false;
		motor.desiredHeading = motor.heading;
	}
#endif
	mQueued = false;
	mControlling = false;
}
void CharacterBehaviorHost::release(const TaskToken& token) {
	if (mControlling && mToken.sameRun(token)) {
		clear();
	}
}
}
