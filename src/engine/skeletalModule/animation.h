#pragma once

#include <string>
#include <stdint.h>
#include <vector>
#include "mathModule/math.h"

#define MAXBONES 128

namespace IKIGAI::SKELETON {
	struct Keyframe {
		MATH::Vector3f translation = {0.0f, 0.0f, 0.0f};
		MATH::QuaternionF rotation = {0.0f, 0.0f, 0.0f, 1.0f};
		MATH::Vector3f scale = {1.0f, 1.0f, 1.0f};
	};

	struct TranslationKey {
		double time;
		MATH::Vector3f translation;
	};

	struct RotationKey {
		double	  time;
		MATH::QuaternionF rotation;
	};

	struct ScaleKey {
		double	  time;
		MATH::Vector3f scale;
	};

	struct AnimationChannel {
		std::string					jointName;
		std::vector<TranslationKey> translationKeyframes;
		std::vector<RotationKey>	rotationKeyframes;
		std::vector<ScaleKey>		scaleKeyframes;
	};

	struct Pose {
		uint32_t numKeyframes;
		Keyframe keyframes[MAXBONES];
	};

	struct PoseTransforms {
		alignas(16) MATH::Matrix4f transforms[MAXBONES];
	};

	class Skeleton;

	struct Animation {
		std::string					  name;
		uint32_t					  keyframeCount;
		std::vector<AnimationChannel> channels;
		double						  duration;
		double						  durationInTicks;
		double						  ticksPerSecond;
	};


	MATH::Vector3f translationDelta(const MATH::Vector3f& reference, MATH::Vector3f additive);
	MATH::Vector3f scaleDelta(const MATH::Vector3f& reference, MATH::Vector3f additive);
	MATH::QuaternionF rotationDelta(const MATH::QuaternionF& reference, MATH::QuaternionF additive);
	std::string trimmedName(const std::string& name);
}