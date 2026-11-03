#pragma once

#include "component.h"
#include "utilsModule/animation.h"
#include "utilsModule/reflection/reflection_macros.h"

#include <memory>
#include <string>
#include <vector>

namespace IKIGAI::ECS {

IKI_CLASS(Groups = [Component])
class PropertyAnimationComponent : public ComponentBase {
	IKI_GENERATED_BODY(PropertyAnimationComponent)
public:
	IKI_CLASS(Name = PropertyAnimationComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(PropertyAnimationComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "PropertyAnimationComponentType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "TimelinePath"))
		std::string TimelinePath;
		IKI_PROPERTY(SEREALIZE(name = "PlayOnStart"))
		bool PlayOnStart = true;
		IKI_PROPERTY(SEREALIZE(name = "LoopOverride"))
		bool LoopOverride = false;
		IKI_PROPERTY(SEREALIZE(name = "Loop"))
		bool Loop = false;
		IKI_PROPERTY(SEREALIZE(name = "TimelineEvents"))
		std::vector<std::string> TimelineEvents;
	};

	PropertyAnimationComponent(UTILS::Ref<ECS::Object> obj);
	PropertyAnimationComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor);
	PropertyAnimationComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: PropertyAnimationComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

	void onEnable() override;
	void onDisable() override;

	void play();
	void pause();
	void stop();
	bool isPlaying() const;
	ANIMATION::Animation* getRuntimeAnimation();
	const ANIMATION::Animation* getRuntimeAnimation() const;

	[[nodiscard]] Descriptor getDescriptor() const;

private:
	void rebuildRuntimeAnimation();

	std::string mTimelinePath;
	bool mPlayOnStart = true;
	bool mLoopOverride = false;
	bool mLoop = false;
	std::vector<std::string> mTimelineEvents;
	std::unique_ptr<ANIMATION::Animation> mAnimation;
	bool mLoaded = false;
};

template <> inline std::string GetType<PropertyAnimationComponent>() {
	return "class IKIGAI::ECS::PropertyAnimationComponent";
}
template <> inline std::string GetComponentName<PropertyAnimationComponent>() {
	return "PropertyAnimationComponent";
}

} // namespace IKIGAI::ECS
