#include "skeleton.h"
#include "stateGraphDescriptor.h"
#include <stdexcept>

namespace IKIGAI::SKELETON {
namespace {
	ClipRootMode parseRootMode(const std::string& mode) {
		if (mode == "in-place") {
			return ClipRootMode::InPlace;
		}
		if (mode == "stairs") {
			return ClipRootMode::Stairs;
		}
		if (mode == "flat") {
			return ClipRootMode::Flat;
		}
		if (mode == "traversal") {
			return ClipRootMode::Traversal;
		}
		if (mode == "authored") {
			return ClipRootMode::Authored;
		}
		throw std::runtime_error("unknown RootMode: " + mode);
	}
}

ClipRootMode parseClipRootMode(const std::string& mode) {
	return parseRootMode(mode);
}

RootMotionPolicy parseRootMotionPolicy(const std::string& policy) {
	if (policy == "in-pose" || policy == "in_pose") {
		return RootMotionPolicy::InPose;
	}
	if (policy == "extract") {
		return RootMotionPolicy::Extract;
	}
	throw std::runtime_error("unknown RootMotion policy: " + policy);
}

AnimationLayerMode parseLayerMode(const std::string& mode) {
	if (mode == "override") {
		return AnimationLayerMode::Override;
	}
	if (mode == "additive") {
		return AnimationLayerMode::Additive;
	}
	throw std::runtime_error("unknown layer Mode: " + mode);
}

uint32_t compileBoneMask(const Skeleton& skeleton, uint32_t explicitMask, const std::vector<std::string>& joints) {
	if (!joints.empty()) {
		uint32_t mask = 0;
		for (const auto& joint : joints) {
			const int index = skeleton.findJointIndex(joint);
			if (index < 0 || index >= 32) {
				throw std::runtime_error("layer mask joint out of range: " + joint);
			}
			mask |= 1u << index;
		}
		return mask ? mask : explicitMask;
	}
	return explicitMask;
}

} // namespace IKIGAI::SKELETON
