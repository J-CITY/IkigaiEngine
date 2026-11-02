#include "audioSystem.h"
#include <audioModule/audioManager.h>
#include <sceneModule/sceneManager.h>

IKIGAI::ECS::AudioSystem::AudioSystem() {
	mName = "AudioSystem";
	mPriority = 30;
	mExclusiveBatch = true;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mReads.insert(cm->getComponentType<AudioListenerComponent>());
	mWrites.insert(cm->getComponentType<AudioComponent>());
}

void IKIGAI::ECS::AudioSystem::onLateUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	// world.getComponentManager()->forEach<ECS::AudioListenerComponent>(
	//	[&](IKIGAI::ECS::Entity e, ECS::AudioListenerComponent& listener) {
	//		if (!listener.getObject().getIsActive()) {
	//			return;
	//		}
	//		AUDIO::AudioManagerAL::GetInstance().setListenerPos(
	//			listener.obj->getTransform()->getWorldPosition(),
	//			listener.obj->getTransform()->getWorldRotation() * -MATH::Vector3f::Forward
	//		);
	//	}
	//);
	// world.getComponentManager()->forEach<ECS::AudioComponent>(
	//	[&](IKIGAI::ECS::Entity e, ECS::AudioComponent& audio) {
	//		if (!audio.res) {
	//			return;
	//		}
	//		if (audio.res->data.is3D) {
	//			AUDIO::AudioManagerAL::GetInstance().setSourcePos(*audio.res, audio.obj->getTransform()->getWorldPosition());
	//		}
	//	}
	//);
	for (auto& listener : world.getComponentManager()->getComponentsArray<ECS::AudioListenerComponent>()) {
		AUDIO::AudioManagerAL::GetInstance().setListenerPos(
			listener.obj->getTransform()->getWorldPosition(), listener.obj->getTransform()->getWorldRotation() * -MATH::Vector3f::Forward);
		break;
	}

	for (auto& audio : world.getComponentManager()->getComponentsArray<ECS::AudioComponent>()) {
		if (!audio.res) {
			continue;
		}
		if (audio.res->data.is3D) {
			AUDIO::AudioManagerAL::GetInstance().setSourcePos(*audio.res, audio.obj->getTransform()->getWorldPosition());
		}
	}
}
