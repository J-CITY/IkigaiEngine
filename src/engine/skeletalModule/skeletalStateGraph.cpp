#include "skeletalStateGraph.h"

#include <iostream>
#include <stdexcept>

namespace IKIGAI::SKELETON {

SkeletalStateGraphInstance::SkeletalStateGraphInstance(
	std::shared_ptr<Skeleton> skeleton, std::shared_ptr<SkeletalStateGraph> graph, std::shared_ptr<UTILS::Environment> env)
	: mSkeleton(skeleton)
	, mGraph(graph)
	, mEnvironment(env) {
	if (!mGraph || !mGraph->definition || !mEnvironment) {
		throw std::runtime_error("State graph instance requires a compiled definition and environment");
	}
	const auto& expected = mGraph->definition->skeleton;
	if (!mSkeleton || !expected || mSkeleton->getNumJolts() != expected->getNumJolts()) {
		throw std::runtime_error("State graph skeleton differs from visual skeleton");
	}
	if (mSkeleton->joints().size() != expected->joints().size()) {
		throw std::runtime_error("State graph joint layout differs from visual skeleton");
	}
	for (size_t i = 0; i < mSkeleton->joints().size(); ++i) {
		if (mSkeleton->joints()[i].name != expected->joints()[i].name || mSkeleton->joints()[i].parentIndex != expected->joints()[i].parentIndex) {
			throw std::runtime_error("State graph joint layout differs from visual skeleton");
		}
	}
	mRuntime = std::make_unique<StateGraphRuntime>(mGraph->definition, mEnvironment);
	mRevision = mGraph->revision;
}

SkeletalStateGraphInstance::~SkeletalStateGraphInstance() = default;

void SkeletalStateGraphInstance::update(float dt) {
	if (mReloadPolicy == ReloadPolicy::Automatic && mRevision != mGraph->revision && !mRuntime->activeState().lockTransitions) {
		std::string error;
		if (!mRuntime->replace(mGraph->definition, error)) {
			std::cerr << "State graph reload rejected: " << error << std::endl;
		}
		mRevision = mGraph->revision;
	}
	mRuntime->update(dt);
}

Pose* SkeletalStateGraphInstance::getPose() {
	return mRuntime->getPose();
}

std::unique_ptr<IAnimationPlayable> SkeletalStateGraphInstance::clone() const {
	auto env = std::make_shared<UTILS::Environment>(mEnvironment->snapshot());
	auto copy = std::make_unique<SkeletalStateGraphInstance>(mSkeleton, mGraph, env);
	copy->mReloadPolicy = mReloadPolicy;
	copy->mRevision = mRevision;
	copy->mRuntime = std::make_unique<StateGraphRuntime>(*mRuntime);
	copy->mEnvironment = copy->mRuntime->environment();
	return copy;
}
}
