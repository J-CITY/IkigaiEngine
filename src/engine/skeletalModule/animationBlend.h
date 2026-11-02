#pragma once

#include "skeleton.h"

namespace IKIGAI::SKELETON {
class AnimBlend {
public:
	AnimBlend(std::shared_ptr<Skeleton> skeleton);
	~AnimBlend();

	Pose* blend(Pose* base, Pose* secondary, float t);
	Pose* blendPartial(Pose* base, Pose* secondary, float t, const std::string& rootJoint);
	Pose* blendAdditive(Pose* base, Pose* secondary, float t);
	Pose* blendPartialAdditive(Pose* base, Pose* secondary, float t, const std::string& rootJoint);
	Pose* blendAdditiveWithReference(Pose* reference, Pose* secondary, float t);
	Pose* blendPartialAdditiveWithReference(Pose* reference, Pose* secondary, float t, const std::string& rootJoint);
	Pose* blendMasked(Pose* base, Pose* secondary, float t, uint32_t boneMask);
	Pose* blendMaskedAdditive(Pose* base, Pose* secondary, float t, uint32_t boneMask);

private:
	std::shared_ptr<Skeleton> mSkeleton;
	Pose mPose;
};
}
