#include "animFabrikIK.h"

namespace {
	IKIGAI::MATH::QuaternionF rotationFromTwoVectors(IKIGAI::MATH::Vector3f u, IKIGAI::MATH::Vector3f v) {
		float normUNormV = sqrt(IKIGAI::MATH::Vector3f::Dot(u, u) * IKIGAI::MATH::Vector3f::Dot(v, v));
		float realPart = normUNormV + IKIGAI::MATH::Vector3f::Dot(u, v);
		IKIGAI::MATH::Vector3f w;

		if (realPart < 1.e-6f * normUNormV) {
			/* If u and v are exactly opposite, rotate 180 degrees
			 * around an arbitrary orthogonal axis. Axis normalisation
			 * can happen later, when we normalise the quaternion. */
			realPart = 0.0f;
			w = abs(u.x) > abs(u.z) ? IKIGAI::MATH::Vector3f(-u.y, u.x, 0.f) : IKIGAI::MATH::Vector3f(0.f, -u.z, u.y);
		} else {
			/* Otherwise, build quaternion the standard way. */
			w = IKIGAI::MATH::Vector3f::Cross(u, v);
		}

		return IKIGAI::MATH::QuaternionF::Normalize(IKIGAI::MATH::QuaternionF(realPart, w.x, w.y, w.z));
	}
}

namespace IKIGAI::SKELETON {
	AnimFabrikIK::AnimFabrikIK(Skeleton* skeleton) : mSkeleton(skeleton) {

	}

	AnimFabrikIK::~AnimFabrikIK() = default;

	PoseTransforms* AnimFabrikIK::solve(MATH::Matrix4f model, PoseTransforms* localTransforms, PoseTransforms* globalTransforms, const MATH::Vector3f& endEffector, const std::string& startBone, const std::string& endBone) {
		for (int i = 0; i < mSkeleton->getNumJolts(); i++) {
			mTransforms.transforms[i] = globalTransforms->transforms[i];
		}
		int32_t startIdx = mSkeleton->findJointIndex(startBone);

		if (startIdx == -1) {
			//LOF_ERROR("FABRIK IK: Requested start bone not found = " + startBone);
			return globalTransforms;
		}

		int32_t endIdx = mSkeleton->findJointIndex(endBone);

		if (endIdx == -1) {
			//LOG_ERROR("FABRIK IK: Requested end bone not found = " + endBone);
			return globalTransforms;
		}

		int32_t localEndIdx = findSourceChainData(model, startIdx, endIdx, globalTransforms);

		for (uint32_t i = 0; i < mIterations; i++) {
			backwardIk(localEndIdx, endEffector);
			forwardIk(localEndIdx, endEffector);
		}

		modifyTransforms(model, startIdx, endIdx, localTransforms);

		return &mTransforms;
	}

	int32_t AnimFabrikIK::findSourceChainData(MATH::Matrix4f model, int32_t startIdx, int32_t endIdx, PoseTransforms* globalTransforms) {
		int32_t count = 0;

		for (int32_t i = startIdx; i <= endIdx; i++) {
			int32_t idx = count++;

			MATH::Matrix4f m = model * globalTransforms->transforms[i];

			mSourceJointPos[idx] = MATH::Vector3f(m(0,3), m(1,3), m(2,3));
			mIterationJointPos[idx] = mSourceJointPos[idx];
		}

		for (int32_t i = 0; i < (count - 1); i++) {
			mBoneLengths[i] = MATH::Vector3f::Length(mSourceJointPos[i] - mSourceJointPos[i + 1]);
		}
		return count - 1;
	}

	void AnimFabrikIK::forwardIk(int32_t endIdx, const MATH::Vector3f& endEffector) {
		for (int32_t i = 0; i <= endIdx; i++) {
			if (i == 0) {
				mIterationJointPos[i] = mSourceJointPos[i];
			} else {
				MATH::Vector3f dir = MATH::Vector3f::Normalize(mIterationJointPos[i] - mIterationJointPos[i - 1]);
				mIterationJointPos[i] = mIterationJointPos[i - 1] + dir * mBoneLengths[i - 1];
			}
		}
	}

	void AnimFabrikIK::backwardIk(int32_t endIdx, const MATH::Vector3f& endEffector) {
		for (int32_t i = endIdx; i >= 0; i--) {
			if (i == endIdx) {
				mIterationJointPos[i] = endEffector;
			} else {
				MATH::Vector3f dir = MATH::Vector3f::Normalize(mIterationJointPos[i] - mIterationJointPos[i + 1]);
				mIterationJointPos[i] = mIterationJointPos[i + 1] + dir * mBoneLengths[i];
			}
		}
	}

	void AnimFabrikIK::modifyTransforms(MATH::Matrix4f model, int32_t startIdx, int32_t endIdx, PoseTransforms* localTransforms) {
		MATH::Matrix4f invModel = MATH::Matrix4f::Inverse(model);

		for (int32_t i = startIdx; i < endIdx; i++) {
			// Calculate the vector pointing from the one joint to the next in the source transforms.
			MATH::Vector3f srcJointStartPos = MATH::Vector3f(mTransforms.transforms[i](0,3), mTransforms.transforms[i](1,3), mTransforms.transforms[i](2,3));
			MATH::Vector3f srcJointEndPos = MATH::Vector3f(mTransforms.transforms[i + 1](0,3), mTransforms.transforms[i + 1](1,3), mTransforms.transforms[i + 1](2,3));
			MATH::Vector3f srcJointDir = MATH::Vector3f::Normalize(MATH::Vector3f(srcJointEndPos) - MATH::Vector3f(srcJointStartPos));

			// Calculate the vector pointing from the one joint to the next in the IK transforms.
			MATH::Vector4f dstJointStartPos = invModel * MATH::Vector4f(mIterationJointPos[i - startIdx], 1.0f);
			MATH::Vector4f dstJointEndPos = invModel * MATH::Vector4f(mIterationJointPos[i - startIdx + 1], 1.0f);
			MATH::Vector3f dstJointDir = MATH::Vector3f::Normalize(MATH::Vector3f(dstJointEndPos.x, dstJointEndPos.y, dstJointEndPos.z) 
				- MATH::Vector3f(dstJointStartPos.x, dstJointStartPos.y, dstJointStartPos.z));

			// Find the quaternion that rotates from source to destination rotations.
			MATH::QuaternionF srcToDstRotation = rotationFromTwoVectors(srcJointDir, dstJointDir);

			// Find the original rotation of the joint.
			MATH::QuaternionF originRotation = MATH::QuaternionF::Normalize(MATH::QuaternionF(mTransforms.transforms[i]));

			// Create a translation matrix from the destination joint position.
			MATH::Matrix4f translation = MATH::Matrix4f(1.0f);
			translation = translation * MATH::Matrix4f::Translation(MATH::Vector3f(dstJointStartPos.x, dstJointStartPos.y, dstJointStartPos.z));

			// Compute the final joint rotation by adding to the original rotation.
			MATH::QuaternionF finalRotation = MATH::QuaternionF::Normalize(srcToDstRotation * originRotation);
			MATH::Matrix4f rotation = MATH::QuaternionF::ToMatrix4(finalRotation);

			// Compute final joint transform.
			mTransforms.transforms[i] = translation * rotation;
		}

		auto& joints = mSkeleton->joints();

		int32_t i = endIdx;

		while (joints[i].parentIndex > startIdx) {
			if (joints[i].parentIndex == -1)
				mTransforms.transforms[i] = localTransforms->transforms[i];
			else
				mTransforms.transforms[i] = mTransforms.transforms[joints[i].parentIndex] * localTransforms->transforms[i];

			i++;
		}
	}
}
