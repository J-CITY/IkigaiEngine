#pragma once

#include "iAnimationPlayable.h"
#include "stateGraphRuntime.h"
#include "utilsModule/environment.h"
#include <memory>

namespace IKIGAI::SKELETON {
class SkeletalStateGraph {
public:
	std::shared_ptr<const StateGraphDefinition> definition;
	unsigned revision = 0;
};

class SkeletalStateGraphInstance : public IAnimationPlayable {
public:
	enum class ReloadPolicy { Automatic, OwnerValidated };
	void setReloadPolicy(ReloadPolicy policy) {
		mReloadPolicy = policy;
	}
	SkeletalStateGraphInstance(
		std::shared_ptr<Skeleton> skeleton, std::shared_ptr<SkeletalStateGraph> graph, std::shared_ptr<UTILS::Environment> env);
	~SkeletalStateGraphInstance() override;

	void update(float dt) override;
	Pose* getPose() override;
	std::unique_ptr<IAnimationPlayable> clone() const override;
	StateGraphRuntime* runtime() const {
		return mRuntime.get();
	}
	const RootMotionSample& rootMotion() const {
		return mRuntime->rootMotion();
	}
	std::vector<AnimationEventDispatch> consumeAnimationEvents() {
		return mRuntime->consumeAnimationEvents();
	}

private:
	ReloadPolicy mReloadPolicy = ReloadPolicy::Automatic;
	std::unique_ptr<StateGraphRuntime> mRuntime;
	unsigned mRevision = 0;
	std::shared_ptr<Skeleton> mSkeleton;
	std::shared_ptr<SkeletalStateGraph> mGraph;
	std::shared_ptr<UTILS::Environment> mEnvironment;
};
}
