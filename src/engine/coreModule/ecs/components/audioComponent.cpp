#include "audioComponent.h"

#include <sceneModule/sceneManager.h>

#include "resourceModule/audioSourceManager.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

AudioComponent::AudioComponent(UTILS::Ref<ECS::Object> _obj, std::string& path)
	: ComponentBase(_obj) {
	__NAME__ = "AudioComponent";
	res = RESOURCES::ServiceManager::Get<RESOURCES::AudioSourceLoader>().CreateFromFile(path);
}

AudioComponent::AudioComponent(UTILS::Ref<ECS::Object> _obj, const Descriptor& descriptor)
	: ComponentBase(_obj) {
	__NAME__ = "AudioComponent";
	if (!descriptor.AudioPath.empty()) {
		const auto prepared = RESOURCES::ServiceManager::Get<RESOURCES::AudioSourceLoader>().loadResource(
			descriptor.AudioPath, descriptor.AudioPath.ends_with(".sound") ? RESOURCES::ELoadingType::RESOURCE : RESOURCES::ELoadingType::FILE);
		// Decode/cache the sample once; every component keeps its own voice.
		if (prepared)
			res = std::make_shared<AUDIO::SoundResource>(prepared->path, prepared->data);
	}
}

AudioComponent::AudioComponent(UTILS::Ref<ECS::Object> _obj)
	: ComponentBase(_obj) {
	__NAME__ = "AudioComponent";
}

std::string AudioComponent::getSourcePath() const {
	if (!res)
		return { };
	return res->path;
}
void AudioComponent::setSourcePath(std::string val) {
	res = RESOURCES::ServiceManager::Get<RESOURCES::AudioSourceLoader>().CreateFromFile(val);
}

void AudioComponent::onStart() {
	if (res && !res->id)
		AUDIO::AudioManagerAL::GetInstance().addSource(*res);
}
void AudioComponent::onDestroy() {
	if (res && res->id)
		AUDIO::AudioManagerAL::GetInstance().stop(*res);
}

std::string AudioComponent::getSoundPath() const {
	return res->data.pathSoundSource;
}
void AudioComponent::setSoundPath(std::string) {
	// TODO
}

bool AudioComponent::getIs3D() const {
	return res->data.is3D;
}
void AudioComponent::setIs3D(bool val) {
	res->data.is3D = val;
	AUDIO::AudioManagerAL::GetInstance().stop(*res);
	AUDIO::AudioManagerAL::GetInstance().play(*res);
}

bool AudioComponent::getIsLooped() const {
	return res->data.isLooped;
}
void AudioComponent::setIsLooped(bool val) {
	res->data.isLooped = val;
	AUDIO::AudioManagerAL::GetInstance().setLoop(*res, val);
}

float AudioComponent::getVolume() const {
	return res->data.volume;
}
void AudioComponent::setVolume(float val) {
	res->data.volume = val;
	AUDIO::AudioManagerAL::GetInstance().setVolume(*res, val);
}

float AudioComponent::getPan() const {
	return res->data.pan;
}
void AudioComponent::setPan(float val) {
	res->data.pan = val;
	AUDIO::AudioManagerAL::GetInstance().setPan(*res, val);
}

AUDIO::SoundStatus AudioComponent::getState() const {
	return res->data.state;
}
void AudioComponent::setState(AUDIO::SoundStatus val) {
	auto prev = res->data.state;
	res->data.state = val;
	switch (res->data.state) {
	case AUDIO::SoundStatus::PLAY: {
		if (prev == AUDIO::SoundStatus::PAUSE) {
			AUDIO::AudioManagerAL::GetInstance().unpause(*res);
		}
		else {
			AUDIO::AudioManagerAL::GetInstance().play(*res);
		}
		break;
	}
	case AUDIO::SoundStatus::PAUSE:
		AUDIO::AudioManagerAL::GetInstance().pause(*res);
		break;
	case AUDIO::SoundStatus::STOP:
		AUDIO::AudioManagerAL::GetInstance().stop(*res);
		break;
	default:;
	}
}

AudioComponent::Descriptor AudioComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<AudioComponent>();
	descriptor.AudioPath = getSourcePath();
	return descriptor;
}

AudioListenerComponent::AudioListenerComponent(UTILS::Ref<ECS::Object> obj)
	: ComponentBase(obj) {
	__NAME__ = "AudioListenerComponent";
}

AudioListenerComponent::Descriptor AudioListenerComponent::getDescriptor() const {
	Descriptor descriptor;
	descriptor.Type = GetType<AudioListenerComponent>();
	return descriptor;
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::AudioComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::AudioListenerComponent>();
	return true;
}();
}
