#include "ragdollSystem.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/componentReference.inl"
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/ragdollComponent.h"
#include "coreModule/ecs/components/skeletalAnimationComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "sceneModule/scene.h"

namespace IKIGAI::ECS {
namespace {
	using Frame = SKELETON::RagdollFrame;
	void setRoot(Object& object, Frame root) {
		if (auto parent = object.getParent()) {
			root.position = MATH::Matrix4f::Inverse(parent->mTransform->getWorldMatrix()) * root.position;
			root.rotation = MATH::QuaternionF::Conjugate(parent->mTransform->getWorldRotation()) * root.rotation;
		}
		object.mTransform->setLocalPosition(root.position);
		object.mTransform->setLocalRotation(root.rotation);
	}
	Frame frame(Object& object) {
		return { object.mTransform->getWorldPosition(), object.mTransform->getWorldRotation() };
	}
	void compatible(const SKELETON::Skeleton& a, const SKELETON::Skeleton& b) {
		if (a.joints().size() != b.joints().size()) {
			throw std::runtime_error("Ragdoll skeleton size mismatch");
		}
		for (std::size_t index = 0; index < a.joints().size(); ++index) {
			const auto& left = a.joints()[index];
			const auto& right = b.joints()[index];
			if (left.name != right.name || left.parentIndex != right.parentIndex
				|| (left.bindPose.translation - right.bindPose.translation).magnitude() > .001f
				|| (left.bindPose.scale - right.bindPose.scale).magnitude() > .001f
				|| std::abs(MATH::QuaternionF::Dot(left.bindPose.rotation, right.bindPose.rotation)) < .999f) {
				throw std::runtime_error("Ragdoll skeleton bind mismatch at " + left.name);
			}
			for (int element = 0; element < 16; ++element) {
				if (std::abs(left.offsetTransform.getData()[element] - right.offsetTransform.getData()[element]) > .001f) {
					throw std::runtime_error("Ragdoll inverse-bind mismatch at " + left.name);
				}
			}
		}
	}
}
bool RagdollSystem::controlsMotor(SCENE_SYSTEM::Scene& scene, CharacterMotorComponent& motor) {
	if (auto ragdoll = motor.obj->getComponent<RagdollComponent>(); ragdoll && ragdoll->state() == RagdollState::Simulated) {
		return true;
	}
	if (!motor.settings.Visual.empty()) {
		if (auto visual = Resolve<TransformComponent>(scene, motor.settings.Visual)) {
			if (auto ragdoll = visual->obj->getComponent<RagdollComponent>()) {
				return ragdoll->state() == RagdollState::Simulated;
			}
		}
	}
	return false;
}
void RagdollSystem::beforePhysics(SCENE_SYSTEM::Scene& scene, float dt) {
	for (auto& object : scene.getObjects()) {
		auto component = object->getComponent<RagdollComponent>();
		if (!component) {
			continue;
		}
		auto& c = *component;
		if (!object->getIsActive()) {
			c.onDisable();
			continue;
		}
		c.mFixedDt = dt;
		c.mRemainder = std::max(0.f, c.mRemainder - dt);
		if (!c.mStarted) {
			c.mActivateRequested |= c.settings.StartSimulated;
			c.mStarted = true;
		}
		if (c.mResetRequested) {
			c.release();
			continue;
		}
		if (c.settings.Definition.empty()) {
			if (c.mActivateRequested) {
				throw std::runtime_error("Ragdoll Definition is empty");
			}
			c.release();
			continue;
		}
		auto resource = RESOURCES::ServiceManager::Get<RESOURCES::RagdollLoader>().loadResource(c.settings.Definition);
		if (!resource || !resource->definition) {
			throw std::runtime_error("Ragdoll resource has no definition");
		}
		auto skeleton = object->getComponent<SkeletalComponent>();
		if (!skeleton || !skeleton->getSkeleton()) {
			throw std::runtime_error("Ragdoll requires SkeletalComponent");
		}
		if (c.mResource != resource || c.mRevision != resource->revision || c.mLoadedPath != c.settings.Definition) {
			try {
				compatible(*skeleton->getSkeleton(), *resource->definition->skeleton);
			} catch (const std::exception& error) {
				if (!c.mDefinition || c.mLoadedPath != c.settings.Definition) {
					throw;
				}
				c.mLastError = error.what();
				c.mRevision = resource->revision;
				continue;
			}
			// A valid resource replacement resets simulation at the next fixed boundary.
			const bool activate = c.mActivateRequested;
			auto impulses = std::move(c.mImpulses);
			c.release();
			c.mImpulses = std::move(impulses);
			c.mActivateRequested = activate;
			c.mResource = resource;
			c.mDefinition = resource->definition;
			c.mLastError.clear();
			c.mRevision = resource->revision;
			c.mLoadedPath = c.settings.Definition;
		}
		compatible(*skeleton->getSkeleton(), *c.mDefinition->skeleton);
		if (c.mActivateRequested && c.state() == RagdollState::Animated) {
			if (object->getComponent<Physics3DComponent>()) {
				throw std::runtime_error("Ragdoll and rigid body cannot drive the same transform");
			}
			const auto scale = object->mTransform->getWorldScale();
			const float units = c.mDefinition->unitsToMeters;
			if ((scale - MATH::Vector3f { units, units, units }).magnitude() > .0001f) {
				throw std::runtime_error("Ragdoll visual world scale must equal UnitsToMeters on every axis");
			}
			for (auto parent = object->getParent(); parent; parent = parent->getParent()) {
				if ((parent->mTransform->getWorldScale() - MATH::Vector3f { 1, 1, 1 }).magnitude() > .0001f) {
					throw std::runtime_error("Ragdoll ancestors require unit scale");
				}
			}
			auto motor
				= c.settings.Motor.empty() ? UTILS::WeakPtr<CharacterMotorComponent> {} : Resolve<CharacterMotorComponent>(scene, c.settings.Motor);
			if (!c.settings.Motor.empty() && !motor) {
				throw std::runtime_error("Ragdoll Motor reference is missing");
			}
			if (motor && motor->obj.get().getID() != object->getID()) {
				auto visual = Resolve<TransformComponent>(scene, motor->settings.Visual);
				if (!visual || visual->obj.get().getID() != object->getID()) {
					throw std::runtime_error("Ragdoll Motor must drive this visual");
				}
			}
			SKELETON::Pose pose {};
			auto root = frame(*object);
			if (c.mSamples) {
				pose = c.mCurrentPose;
				root = c.mCurrentRoot;
			}
			else if (auto animator = object->getComponent<SkeletalAnimationComponent>(); animator && animator->getPlayable()) {
				pose = *animator->getPlayable()->getPose();
			}
			else {
				pose.numKeyframes = static_cast<std::uint32_t>(skeleton->getSkeleton()->joints().size());
				for (std::size_t index = 0; index < pose.numKeyframes; ++index) {
					pose.keyframes[index] = skeleton->getSkeleton()->joints()[index].bindPose;
				}
			}
			auto instance = std::make_shared<SKELETON::RagdollInstance>(c.mDefinition);
			instance->activate(
				scene.physics3dShared(), pose, root, c.mSamples > 1 ? &c.mPreviousPose : nullptr, c.mSamples > 1 ? &c.mPreviousRoot : nullptr, dt);
			c.mSavedLocalPosition = object->mTransform->getLocalPosition();
			c.mSavedLocalRotation = object->mTransform->getLocalRotation();
			if (motor) {
				c.mMotor = UTILS::WeakPtr<ComponentBase>(motor.get());
				c.mSavedMotorPosition = motor->initialized ? motor->motor.capsule.feet : motor->obj->mTransform->getWorldPosition();
			}
			skeleton->setPoseOverride(instance);
			c.mInstance = std::move(instance);
			c.mRemainder = 0;
		}
		c.mActivateRequested = false;
		if (c.state() == RagdollState::Simulated) {
			for (const auto& pending : c.mImpulses) {
				c.mInstance->applyImpulse(pending.segment, pending.impulse, pending.point);
			}
		}
		else if (!c.mImpulses.empty()) {
			throw std::runtime_error("Ragdoll impulse requires an active or requested simulation");
		}
		c.mImpulses.clear();
	}
}
void RagdollSystem::afterPhysics(SCENE_SYSTEM::Scene& scene) {
	for (auto& object : scene.getObjects()) {
		if (auto c = object->getComponent<RagdollComponent>(); object->getIsActive() && c && c->state() == RagdollState::Simulated) {
			c->mInstance->capture();
			if (!c->mInstance->simulated()) {
				c->release();
				continue;
			}
			c->mInstance->interpolate(1);
			setRoot(*object, c->mInstance->simulationRoot());
		}
	}
}
void RagdollSystem::afterAnimation(SCENE_SYSTEM::Scene& scene, float dt) {
	for (auto& object : scene.getObjects()) {
		auto c = object->getComponent<RagdollComponent>();
		if (!c || !object->getIsActive() || c->settings.Definition.empty() || c->state() == RagdollState::Simulated) {
			continue;
		}
		auto animator = object->getComponent<SkeletalAnimationComponent>();
		if (!animator || !animator->getPlayable()) {
			continue;
		}
		animator->advanceFixed(dt);
		c->mPreviousPose = c->mCurrentPose;
		c->mPreviousRoot = c->mCurrentRoot;
		c->mCurrentPose = *animator->getPlayable()->getPose();
		c->mCurrentRoot = frame(*object);
		if (!c->settings.Motor.empty()) {
			if (auto motor = Resolve<CharacterMotorComponent>(scene, c->settings.Motor); motor && motor->initialized) {
				c->mCurrentRoot = { motor->motor.capsule.feet, MATH::QuaternionF(MATH::Vector3f { 0, motor->heading, 0 }) };
			}
		}
		c->mSamples = std::min(2u, c->mSamples + 1);
	}
}
void RagdollSystem::update(SCENE_SYSTEM::Scene& scene, float dt) {
	for (auto& object : scene.getObjects()) {
		if (auto c = object->getComponent<RagdollComponent>(); object->getIsActive() && c && c->state() == RagdollState::Simulated) {
			c->mRemainder += dt;
			c->mInstance->interpolate(c->mRemainder / c->mFixedDt);
			setRoot(*object, c->mInstance->root());
			if (c->mMotor) {
				auto& motor = static_cast<CharacterMotorComponent&>(*c->mMotor);
				motor.rendered = c->mInstance->root().position;
			}
			if (auto model = object->getComponent<ModelRenderer>()) {
				scene.updateInBVH(UTILS::WeakPtr<ComponentBase>(model.get()));
			}
		}
	}
}
}
#endif
