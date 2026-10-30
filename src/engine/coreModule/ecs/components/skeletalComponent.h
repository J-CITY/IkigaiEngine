#pragma once

#include "component.h"
#include "skeletalModule/animationOffset.h"
#include "skeletalModule/animationTransform.h"
#include "skeletalModule/iAnimationPlayable.h"
#include "skeletalModule/skeleton.h"
#include "utilsModule/reflection/reflection_macros.h"

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class SkeletalComponent : public ComponentBase {
	IKI_GENERATED_BODY(SkeletalComponent)
public:
	IKI_CLASS(Name = SkeletalComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(SkeletalComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "SkeletalComponentType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "SkeletalPath"))
		std::string SkeletalPath;
	};
	SkeletalComponent(UTILS::Ref<ECS::Object> obj);
	SkeletalComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& _descriptor);
	SkeletalComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: SkeletalComponent(obj, static_cast<const Descriptor&>(descriptor)) { };
	~SkeletalComponent() override;

	void setSkeleton(const std::string& path);
	std::shared_ptr<SKELETON::Skeleton> getSkeleton() const {
		return mSkeleton;
	}
	SKELETON::IAnimationPlayable* finalPlayable(SKELETON::IAnimationPlayable* animation) const;
	void setPoseOverride(std::shared_ptr<SKELETON::IAnimationPlayable>);
	void clearPoseOverride(const SKELETON::IAnimationPlayable*);
	[[nodiscard]] Descriptor getDescriptor() const;

	// template <class Context>
	// constexpr static auto serde(Context& context, SkeletalComponent& value) {
	//	using namespace serde::attribute;
	//	serde::serde_struct(context, value)
	//		.field(&SkeletalComponent::mSkeletonPath, "SkeletonPath", default_{std::string()});
	// }
	virtual void onDeserialize(nlohmann::json& j) override;
	virtual void onSerialize(nlohmann::json& j) override;

private:
	std::string mSkeletonPath;
	std::shared_ptr<SKELETON::Skeleton> mSkeleton;
	std::shared_ptr<SKELETON::IAnimationPlayable> mPoseOverride {};

public:
	std::shared_ptr<SKELETON::AnimOffset> mAnimOffset;
	std::shared_ptr<SKELETON::AnimLocalTransform> mAnimLocalTransform;
	std::shared_ptr<SKELETON::AnimGlobalTransform> mAnimGlobalTransform;

	static auto GetMembers() {
		return std::tuple {};
	}
};

template <> inline std::string GetType<SkeletalComponent>() {
	return "class IKIGAI::ECS::SkeletalComponent";
}
template <> inline std::string GetComponentName<SkeletalComponent>() {
	return "SkeletalComponent";
}
}

#include "generated/skeletalComponent.generated.h"
