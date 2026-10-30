#include "skeletalComponent.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/skeletonManager.h"

namespace IKIGAI::ECS {
SkeletalComponent::SkeletalComponent(UTILS::Ref<ECS::Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "SkeletalComponent";
}

SkeletalComponent::SkeletalComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& _descriptor)
	: ComponentBase(obj) {
	__NAME__ = "SkeletalComponent";
	setSkeleton(_descriptor.SkeletalPath);
}

SkeletalComponent::~SkeletalComponent() = default;

SKELETON::IAnimationPlayable* SkeletalComponent::finalPlayable(SKELETON::IAnimationPlayable* animation) const {
	return mPoseOverride ? mPoseOverride.get() : animation;
}
void SkeletalComponent::setPoseOverride(std::shared_ptr<SKELETON::IAnimationPlayable> pose) {
	if (mPoseOverride && mPoseOverride != pose) {
		throw std::logic_error("Skeleton already has a pose override owner");
	}
	mPoseOverride = std::move(pose);
}
void SkeletalComponent::clearPoseOverride(const SKELETON::IAnimationPlayable* owner) {
	if (mPoseOverride.get() == owner) {
		mPoseOverride.reset();
	}
}

void SkeletalComponent::setSkeleton(const std::string& path) {
	mSkeletonPath = path;
	if (!path.empty()) {
		// TODO: calculate res file or resource
		mSkeleton = RESOURCES::ServiceManager::Get<RESOURCES::SkeletonLoader>().loadResource(path, RESOURCES::ELoadingType::FILE);
	}
	else {
		mSkeleton = nullptr;
	}

	if (mSkeleton) {
		mAnimOffset = std::make_shared<SKELETON::AnimOffset>(mSkeleton.get());
		mAnimLocalTransform = std::make_shared<SKELETON::AnimLocalTransform>(mSkeleton.get());
		mAnimGlobalTransform = std::make_shared<SKELETON::AnimGlobalTransform>(mSkeleton.get());
	}
	else {
		mAnimOffset = nullptr;
		mAnimLocalTransform = nullptr;
		mAnimGlobalTransform = nullptr;
	}
}

SkeletalComponent::Descriptor SkeletalComponent::getDescriptor() const {
	Descriptor desc;
	desc.Type = GetComponentName<SkeletalComponent>();
	desc.SkeletalPath = mSkeletonPath;
	return desc;
}

void SkeletalComponent::onDeserialize(nlohmann::json& j) {
	if (j.contains("SkeletonPath")) {
		setSkeleton(j["SkeletonPath"].get<std::string>());
	}
}

void SkeletalComponent::onSerialize(nlohmann::json& j) {
	j["SkeletonPath"] = mSkeletonPath;
}
}
