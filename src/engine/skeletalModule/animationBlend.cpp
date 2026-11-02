#include "animationBlend.h"

namespace IKIGAI::SKELETON {
AnimBlend::AnimBlend(std::shared_ptr<Skeleton> skeleton)
	: mSkeleton(skeleton) { }

AnimBlend::~AnimBlend() = default;

Pose* AnimBlend::blend(Pose* base, Pose* secondary, float t) {
	mPose.numKeyframes = base->numKeyframes;

	for (uint32_t i = 0; i < base->numKeyframes; i++) {
		mPose.keyframes[i].translation = MATH::Vector3f::Lerp(base->keyframes[i].translation, secondary->keyframes[i].translation, t);
		mPose.keyframes[i].rotation = MATH::QuaternionF::Slerp(base->keyframes[i].rotation, secondary->keyframes[i].rotation, t);
		mPose.keyframes[i].scale = MATH::Vector3f::Lerp(base->keyframes[i].scale, secondary->keyframes[i].scale, t);
	}

	return &mPose;
}

Pose* AnimBlend::blendPartial(Pose* base, Pose* secondary, float t, const std::string& rootJoint) {
	mPose.numKeyframes = base->numKeyframes;

	uint32_t idx = mSkeleton->findJointIndex(rootJoint);
	bool found = false;
	auto& joints = mSkeleton->joints();

	if (idx >= 0) {
		for (uint32_t i = 0; i < base->numKeyframes; i++) {
			if (i == idx) {
				found = true;
			}
			if (i > idx && joints[i].parentIndex < idx) {
				found = false;
			}

			if (found && (joints[i].parentIndex >= idx || i == idx)) {
				mPose.keyframes[i].translation = MATH::Vector3f::Lerp(base->keyframes[i].translation, secondary->keyframes[i].translation, t);
				mPose.keyframes[i].rotation = MATH::QuaternionF::Slerp(base->keyframes[i].rotation, secondary->keyframes[i].rotation, t);
				mPose.keyframes[i].scale = MATH::Vector3f::Lerp(base->keyframes[i].scale, secondary->keyframes[i].scale, t);
			}
			else {
				mPose.keyframes[i].translation = base->keyframes[i].translation;
				mPose.keyframes[i].rotation = base->keyframes[i].rotation;
				mPose.keyframes[i].scale = base->keyframes[i].scale;
			}
		}
	}

	return &mPose;
}

Pose* AnimBlend::blendAdditive(Pose* base, Pose* secondary, float t) {
	mPose.numKeyframes = base->numKeyframes;

	for (uint32_t i = 0; i < base->numKeyframes; i++) {
		mPose.keyframes[i].translation = base->keyframes[i].translation + secondary->keyframes[i].translation * t;
		mPose.keyframes[i].rotation
			= base->keyframes[i].rotation * MATH::QuaternionF::Slerp(MATH::QuaternionF(1.0f, 0.0f, 0.0f, 0.0f), secondary->keyframes[i].rotation, t);
		mPose.keyframes[i].scale = base->keyframes[i].scale + secondary->keyframes[i].scale * t;
	}

	return &mPose;
}

Pose* AnimBlend::blendPartialAdditive(Pose* base, Pose* secondary, float t, const std::string& rootJoint) {
	mPose.numKeyframes = base->numKeyframes;

	uint32_t idx = mSkeleton->findJointIndex(rootJoint);
	bool found = false;
	auto& joints = mSkeleton->joints();

	if (idx >= 0) {
		for (uint32_t i = 0; i < base->numKeyframes; i++) {
			if (i == idx) {
				found = true;
			}
			if (i > idx && joints[i].parentIndex < idx) {
				found = false;
			}
			if (found && (joints[i].parentIndex >= idx || i == idx)) {
				mPose.keyframes[i].translation = base->keyframes[i].translation + secondary->keyframes[i].translation * t;
				mPose.keyframes[i].rotation = base->keyframes[i].rotation
					* MATH::QuaternionF::Slerp(MATH::QuaternionF(1.0f, 0.0f, 0.0f, 0.0f), secondary->keyframes[i].rotation, t);
				mPose.keyframes[i].scale = base->keyframes[i].scale + secondary->keyframes[i].scale * t;
			}
			else {
				mPose.keyframes[i].translation = base->keyframes[i].translation;
				mPose.keyframes[i].rotation = base->keyframes[i].rotation;
				mPose.keyframes[i].scale = base->keyframes[i].scale;
			}
		}
	}

	return &mPose;
}

Pose* AnimBlend::blendAdditiveWithReference(Pose* reference, Pose* secondary, float t) {
	mPose.numKeyframes = reference->numKeyframes;

	for (uint32_t i = 0; i < reference->numKeyframes; i++) {
		MATH::Vector3f deltaTranslation = translationDelta(reference->keyframes[i].translation, secondary->keyframes[i].translation);
		MATH::QuaternionF deltaRotation = rotationDelta(reference->keyframes[i].rotation, secondary->keyframes[i].rotation);
		MATH::Vector3f deltaScale = scaleDelta(reference->keyframes[i].scale, secondary->keyframes[i].scale);

		mPose.keyframes[i].translation = reference->keyframes[i].translation + deltaTranslation * t;
		mPose.keyframes[i].rotation
			= reference->keyframes[i].rotation * MATH::QuaternionF::Slerp(MATH::QuaternionF(1.0f, 0.0f, 0.0f, 0.0f), deltaRotation, t);
		mPose.keyframes[i].scale = reference->keyframes[i].scale + deltaScale * t;
	}

	return &mPose;
}

Pose* AnimBlend::blendPartialAdditiveWithReference(Pose* reference, Pose* secondary, float t, const std::string& rootJoint) {
	mPose.numKeyframes = reference->numKeyframes;

	uint32_t idx = mSkeleton->findJointIndex(rootJoint);
	bool found = false;
	auto& joints = mSkeleton->joints();

	if (idx >= 0) {
		for (uint32_t i = 0; i < reference->numKeyframes; i++) {
			if (i == idx) {
				found = true;
			}
			if (i > idx && joints[i].parentIndex < idx) {
				found = false;
			}

			if (found && (joints[i].parentIndex >= idx || i == idx)) {
				MATH::Vector3f deltaTranslation = translationDelta(reference->keyframes[i].translation, secondary->keyframes[i].translation);
				MATH::QuaternionF deltaRotation = rotationDelta(reference->keyframes[i].rotation, secondary->keyframes[i].rotation);
				MATH::Vector3f deltaScale = scaleDelta(reference->keyframes[i].scale, secondary->keyframes[i].scale);

				mPose.keyframes[i].translation = reference->keyframes[i].translation + deltaTranslation * t;
				mPose.keyframes[i].rotation
					= reference->keyframes[i].rotation * MATH::QuaternionF::Slerp(MATH::QuaternionF(1.0f, 0.0f, 0.0f, 0.0f), deltaRotation, t);
				mPose.keyframes[i].scale = reference->keyframes[i].scale + deltaScale * t;
			}
			else {
				mPose.keyframes[i].translation = reference->keyframes[i].translation;
				mPose.keyframes[i].rotation = reference->keyframes[i].rotation;
				mPose.keyframes[i].scale = reference->keyframes[i].scale;
			}
		}
	}

	return &mPose;
}

Pose* AnimBlend::blendMasked(Pose* base, Pose* secondary, float t, uint32_t boneMask) {
	mPose.numKeyframes = base->numKeyframes;
	for (uint32_t i = 0; i < base->numKeyframes; ++i) {
		if (boneMask & (1u << i)) {
			mPose.keyframes[i].translation = MATH::Vector3f::Lerp(base->keyframes[i].translation, secondary->keyframes[i].translation, t);
			mPose.keyframes[i].rotation = MATH::QuaternionF::Slerp(base->keyframes[i].rotation, secondary->keyframes[i].rotation, t);
			mPose.keyframes[i].scale = MATH::Vector3f::Lerp(base->keyframes[i].scale, secondary->keyframes[i].scale, t);
		}
		else {
			mPose.keyframes[i] = base->keyframes[i];
		}
	}
	return &mPose;
}

Pose* AnimBlend::blendMaskedAdditive(Pose* base, Pose* secondary, float t, uint32_t boneMask) {
	mPose.numKeyframes = base->numKeyframes;
	for (uint32_t i = 0; i < base->numKeyframes; ++i) {
		if (boneMask & (1u << i)) {
			mPose.keyframes[i].translation = base->keyframes[i].translation + secondary->keyframes[i].translation * t;
			mPose.keyframes[i].rotation = base->keyframes[i].rotation
				* MATH::QuaternionF::Slerp(MATH::QuaternionF(1.0f, 0.0f, 0.0f, 0.0f), secondary->keyframes[i].rotation, t);
			mPose.keyframes[i].scale = base->keyframes[i].scale + secondary->keyframes[i].scale * t;
		}
		else {
			mPose.keyframes[i] = base->keyframes[i];
		}
	}
	return &mPose;
}
}
