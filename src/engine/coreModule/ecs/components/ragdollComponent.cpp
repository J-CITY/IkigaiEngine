#include "ragdollComponent.h"
#if USE_PHYSICS_3D
#include "characterMotorComponent.h"
#include "coreModule/ecs/object.h"
#include "skeletalAnimationComponent.h"
#include "skeletalComponent.h"
#include "skeletalStateGraphComponent.h"
#include "traversalComponent.h"

namespace IKIGAI::ECS {
RagdollComponent::RagdollComponent(UTILS::Ref<Object> owner)
	: ComponentBase(owner) {
	__NAME__ = "RagdollComponent";
	settings.Type = GetType<RagdollComponent>();
}
RagdollComponent::RagdollComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor& descriptor)
	: RagdollComponent(owner) {
	settings = static_cast<const Descriptor&>(descriptor);
}
RagdollComponent::~RagdollComponent() {
	release();
}
RagdollComponent::Descriptor RagdollComponent::getDescriptor() const {
	return settings;
}
void RagdollComponent::activate() {
	mActivateRequested = true;
	mResetRequested = false;
}
void RagdollComponent::reset() {
	mResetRequested = true;
	mActivateRequested = false;
	mImpulses.clear();
}
void RagdollComponent::applyImpulse(std::size_t segment, MATH::Vector3f impulse, MATH::Vector3f point) {
	if (!PHYSICS_COMMON::finite(impulse) || !PHYSICS_COMMON::finite(point)) {
		throw std::invalid_argument("Invalid ragdoll impulse");
	}
	mImpulses.push_back({ segment, impulse, point });
}
RagdollState RagdollComponent::state() const {
	return mInstance && mInstance->simulated() ? RagdollState::Simulated : RagdollState::Animated;
}
const std::string& RagdollComponent::lastError() const {
	return mLastError;
}
void RagdollComponent::release() {
	if (mInstance) {
		if (auto skeleton = obj->getComponent<SkeletalComponent>()) {
			skeleton->clearPoseOverride(mInstance.get());
		}
		obj->mTransform->setLocalPosition(mSavedLocalPosition);
		obj->mTransform->setLocalRotation(mSavedLocalRotation);
		if (mMotor) {
			auto& motor = static_cast<CharacterMotorComponent&>(*mMotor);
			motor.motor.reset(mSavedMotorPosition);
			motor.previous = motor.rendered = mSavedMotorPosition;
			motor.wish = {};
			motor.jump = false;
			motor.remainder = 0;
			if (auto traversal = motor.obj->getComponent<TraversalComponent>()) {
				traversal->traversing = false;
				traversal->poseOffset = {};
			}
		}
		if (auto graph = obj->getComponent<SkeletalStateGraphComponent>()) {
			graph->onDisable();
		}
		else if (auto animator = obj->getComponent<SkeletalAnimationComponent>(); animator && !animator->getStateGraph().empty()) {
			animator->setAnimation(animator->getStateGraph());
		}
	}
	mInstance.reset();
	mMotor = nullptr;
	mSamples = 0;
	mImpulses.clear();
	mActivateRequested = false;
	mResetRequested = false;
}
void RagdollComponent::onDisable() {
	release();
	mStarted = false;
}
void RagdollComponent::onDestroy() {
	onDisable();
}
}
#endif
