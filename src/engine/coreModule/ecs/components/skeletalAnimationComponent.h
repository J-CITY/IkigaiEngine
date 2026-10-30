#pragma once

#include "component.h"
#include "skeletalModule/iAnimationPlayable.h"
#include "utilsModule/environment.h"
#include "utilsModule/reflection/reflection_macros.h"
#include <memory>

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class SkeletalAnimationComponent : public ComponentBase {
	IKI_GENERATED_BODY(SkeletalAnimationComponent)
public:
	IKI_CLASS(Name = SkeletalAnimationComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(SkeletalAnimationComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "SkeletalAnimationComponentType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "SkeletalPlayablePath"))
		std::string SkeletalPlayablePath;
	};
	SkeletalAnimationComponent(UTILS::Ref<ECS::Object> obj);
	SkeletalAnimationComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& _descriptor);
	SkeletalAnimationComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: SkeletalAnimationComponent(obj, static_cast<const Descriptor&>(descriptor)) { };
	~SkeletalAnimationComponent() override;

	void onUpdate(std::chrono::duration<double> dt) override;
	void advanceFixed(float dt);

	void setPlayable(std::shared_ptr<SKELETON::IAnimationPlayable> playable);
	void claimPlayback(ComponentBase& owner);
	void releasePlayback(ComponentBase& owner);
	SKELETON::IAnimationPlayable* getPlayable() const {
		return mPlayable.get();
	}
	std::shared_ptr<SKELETON::IAnimationPlayable> getPlayableShared() const {
		return mPlayable;
	}

	std::shared_ptr<UTILS::Environment> getEnvironment() const {
		return mEnvironment;
	}
	[[nodiscard]] Descriptor getDescriptor() const;

	// template <class Context>
	// constexpr static auto serde(Context& context, SkeletalAnimationComponent& value) {
	//	using namespace serde::attribute;
	//	serde::serde_struct(context, value)
	//		.field(&SkeletalAnimationComponent::mGraphPath, "GraphPath", default_{std::string()});
	// }
	void setAnimation(const std::string& playable);
	void setStateGraph(const std::string& path);
	std::string getStateGraph() const {
		return mGraphPath;
	}

	virtual void onDeserialize(nlohmann::json& j) override;
	virtual void onSerialize(nlohmann::json& j) override;

private:
	UTILS::WeakPtr<ComponentBase> mPlaybackOwner;
	std::string mGraphPath;
	std::shared_ptr<SKELETON::IAnimationPlayable> mPlayable;
	// TODO: move it somewhare
	std::shared_ptr<UTILS::Environment> mEnvironment;

public:
	static auto GetMembers() {
		return std::tuple {};
	}
};

template <> inline std::string GetType<SkeletalAnimationComponent>() {
	return "class IKIGAI::ECS::SkeletalAnimationComponent";
}
template <> inline std::string GetComponentName<SkeletalAnimationComponent>() {
	return "SkeletalAnimationComponent";
}
}

#include "generated/skeletalAnimationComponent.generated.h"
