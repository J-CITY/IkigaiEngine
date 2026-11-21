#pragma once
#include "component.h"
#include "reflectionModule/reflection_macros.h"
#include <audioModule/audioManager.h>


#include "pointLight.h"

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
IKI_CLASS(Groups = [Component])
class AudioComponent : public ComponentBase {
	IKI_GENERATED_BODY(AudioComponent)
public:
	IKI_CLASS(Name = AudioComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(AudioComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "AudioComponentType"))
		std::string Type;
		IKI_PROPERTY(SEREALIZE(name = "AudioPath"))
		std::string AudioPath;
	};
	AudioComponent(UTILS::Ref<ECS::Object> _obj);
	void onStart() override;
	void onDestroy() override;

	AudioComponent(UTILS::Ref<ECS::Object> _obj, std::string& path);
	AudioComponent(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor);
	AudioComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: AudioComponent(obj, static_cast<const Descriptor&>(descriptor)) { };

	std::string getSourcePath() const;
	void setSourcePath(std::string val);

	std::string getSoundPath() const;
	void setSoundPath(std::string);

	bool getIs3D() const;
	void setIs3D(bool val);

	bool getIsLooped() const;
	void setIsLooped(bool val);

	float getVolume() const;
	void setVolume(float val);

	float getPan() const;
	void setPan(float val);

	AUDIO::SoundStatus getState() const;
	void setState(AUDIO::SoundStatus val);

	std::shared_ptr<AUDIO::SoundResource> res;

	[[nodiscard]] Descriptor getDescriptor() const;

public:
	IKI_PROPERTY(Name = SourcePath, Type = std::string, Getter = getSourcePath, Setter = setSourcePath, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = STRING)
	IKI_PROPERTY(Name = Sound, Type = std::string, Getter = getSoundPath, Setter = setSoundPath, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = STRING)
	IKI_PROPERTY(Name = Is3D, Type = bool, Getter = getIs3D, Setter = setIs3D, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR], EditWidget = BOOL)
	IKI_PROPERTY(Name = Volume, Type = float, Getter = getVolume, Setter = setVolume, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditRange = MATH::Vector2f { 0.0f, 1.0f }, EditStep = 0.01f, EditWidget = DRAG_FLOAT)
	IKI_PROPERTY(Name = Pan, Type = float, Getter = getPan, Setter = setPan, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditRange = MATH::Vector2f { 0.0f, 100.0f }, EditStep = 1.0f, EditWidget = DRAG_FLOAT)
	IKI_PROPERTY(Name = State, Type = AUDIO::SoundStatus, Getter = getState, Setter = setState, Flags = [USE_IN_EDITOR_COMPONENT_INSPECTOR],
		EditWidget = COMBO)
	static auto GetMembers() {
		return std::tuple { IKIGAI::UTILS::MakeMemberInfo("SourcePath", &AudioComponent::getSourcePath, &AudioComponent::setSourcePath,
								UTILS::Meta_t {
								{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
								{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING },
								}),
			IKIGAI::UTILS::MakeMemberInfo("Sound", &AudioComponent::getSoundPath, &AudioComponent::setSoundPath,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::STRING },
				}),
			IKIGAI::UTILS::MakeMemberInfo("Is3D", &AudioComponent::getIs3D, &AudioComponent::setIs3D,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::BOOL },
				}),
			IKIGAI::UTILS::MakeMemberInfo("Volume", &AudioComponent::getVolume, &AudioComponent::setVolume,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f { 0.0f, 1.0f } },
				{ UTILS::MetaParam::EDIT_STEP, 0.01f },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT },
				}),
			IKIGAI::UTILS::MakeMemberInfo("Pan", &AudioComponent::getPan, &AudioComponent::setPan,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_RANGE, MATH::Vector2f { 0.0f, 100.0f } },
				{ UTILS::MetaParam::EDIT_STEP, 1.0f },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::DRAG_FLOAT },
				}),
			IKIGAI::UTILS::MakeMemberInfo("State", &AudioComponent::getState, &AudioComponent::setState,
				UTILS::Meta_t {
				{ UTILS::MetaParam::FLAGS, UTILS::MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR },
				{ UTILS::MetaParam::EDIT_WIDGET, UTILS::WidgetType::COMBO },
				}) };
	}
};

IKI_CLASS(Groups = [Component])
class AudioListenerComponent : public ComponentBase {
	IKI_GENERATED_BODY(AudioListenerComponent)
public:
	IKI_CLASS(Name = AudioListenerComponent::Descriptor)
	struct Descriptor : public ComponentBase::Descriptor {
		IKI_GENERATED_BODY(AudioListenerComponent::Descriptor)
		IKI_PROPERTY(SEREALIZE(name = "AudioListenerComponentType"))
		std::string Type;
	};
	AudioListenerComponent(UTILS::Ref<ECS::Object> obj);
	AudioListenerComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
		: AudioListenerComponent(obj) { };

	[[nodiscard]] Descriptor getDescriptor() const;

public:
	static auto GetMembers() {
		return std::tuple { };
	}
};

template <> inline std::string ECS::GetType<AudioListenerComponent>() {
	return "class IKIGAI::ECS::AudioListenerComponent";
}
template <> inline std::string ECS::GetType<AudioComponent>() {
	return "class IKIGAI::ECS::AudioComponent";
}

template <> inline std::string IKIGAI::ECS::GetComponentName<AudioListenerComponent>() {
	return "AudioListenerComponent";
}
template <> inline std::string IKIGAI::ECS::GetComponentName<AudioComponent>() {
	return "AudioComponent";
}
}

#include "generated/audioComponent.generated.h"
