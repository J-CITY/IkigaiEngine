#include "audioManager.h"

#include "resourceModule/fileSystem/fileSystem.h"
#ifdef __EMSCRIPTEN__
#include <iostream>
#endif

IKIGAI::AUDIO::AudioManagerAL::AudioManagerAL() {
	audioEngine = std::make_unique<SoLoud::Soloud>();
	const auto res = audioEngine->init();
	mReady = (res == SoLoud::SO_NO_ERROR);
#ifdef __EMSCRIPTEN__
	std::cout << "[web] SoLoud init result=" << res << " ready=" << mReady << std::endl;
	std::cout.flush();
#endif
}

IKIGAI::AUDIO::AudioManagerAL::~AudioManagerAL() {
	if (mReady && audioEngine) {
		audioEngine->deinit();
	}
}

unsigned IKIGAI::AUDIO::AudioManagerAL::playBus(SoLoud::Bus& bus) {
	if (!isReady()) {
		return 0;
	}
	return audioEngine->play(bus);
}

void IKIGAI::AUDIO::AudioManagerAL::addSource(SoundResource& source) {
	if (!isReady()) {
		return;
	}
	if (source.data.type == SoundType::SOUND) {
		if (!sounds.contains(source.data.pathSoundSource)) {
			sounds[source.data.pathSoundSource] = SoLoud::Wav();
			auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
			auto path = fs.getFilePath(source.data.pathSoundSource);
			if (!path) {
				//problem
			}
			auto res = sounds[source.data.pathSoundSource].load(path.value().c_str());
			if (!res) {
				//problem
			}
		}
		auto& src = sounds[source.data.pathSoundSource];
		if (source.data.is3D) {
			source.id = audioEngine->play3d(src, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, source.data.volume, source.data.state == SoundStatus::PLAY, source.getBusId());
		}
		else {
			source.id = audioEngine->play(src, source.data.volume, source.data.pan, source.data.state != SoundStatus::PLAY, source.getBusId());
		}
	}
	else {
		if (!musics.contains(source.data.pathSoundSource)) {
			musics[source.data.pathSoundSource] = SoLoud::WavStream();

			auto& fs = IKIGAI::RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
			auto path = fs.getFilePath(source.data.pathSoundSource);
			if (!path) {
				//problem
			}
			auto res = musics[source.data.pathSoundSource].load(path.value().c_str());
			if (!res) {
				//problem
			}
		}
		auto& src = musics[source.data.pathSoundSource];
		if (source.data.type == SoundType::BACKGROUND) {
			source.id = audioEngine->playBackground(src, source.data.volume, source.data.state != SoundStatus::PLAY, source.getBusId());
		}
		else {
			if (source.data.is3D) {
				source.id = audioEngine->play3d(src, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, source.data.volume, source.data.state != SoundStatus::PLAY, source.getBusId());
				//TODO: add to config
				audioEngine->set3dSourceMinMaxDistance(source.id.value(), 0.0f, 100.0f);
				audioEngine->set3dSourceAttenuation(source.id.value(), SoLoud::AudioSource::LINEAR_DISTANCE, 1.0f);

				audioEngine->update3dAudio();
			}
			else {
				source.id = audioEngine->play(src, source.data.volume, source.data.pan, source.data.state != SoundStatus::PLAY, source.getBusId());
			}
		}
	}
	audioEngine->setLooping(source.id.value(), source.data.isLooped);
}

void IKIGAI::AUDIO::AudioManagerAL::removeSource(SoundResource& source) {
	auto& id = source.id;
	if (!id) {
		return;
	}
	audioEngine->stop(*id);

	if (musics.contains(source.path)) {
		musics.erase(source.path);
	}
	if (sounds.contains(source.path)) {
		sounds.erase(source.path);
	}
}

void IKIGAI::AUDIO::AudioManagerAL::set3dSoundSpeedSpeed(float speed) {
	audioEngine->set3dSoundSpeed(speed);
}

void IKIGAI::AUDIO::AudioManagerAL::setListenerPos(MATH::Vector3f pos, MATH::Vector3f dir) {
	if (!isReady()) {
		return;
	}
	audioEngine->set3dListenerPosition(-pos.x, pos.y, pos.z);
	audioEngine->set3dListenerAt(dir.x, dir.y, dir.z);
	audioEngine->update3dAudio();
}

void IKIGAI::AUDIO::AudioManagerAL::setSourcePos(SoundResource& source, MATH::Vector3f pos) {
	if (!isReady() || !source.id) {
		return;
	}
	audioEngine->set3dSourcePosition(source.id.value(), pos.x, pos.y, pos.z);
	audioEngine->update3dAudio();
}

void IKIGAI::AUDIO::AudioManagerAL::setListenerVelocity(MATH::Vector3f vel) {
	audioEngine->set3dListenerVelocity(vel.x, vel.y, vel.z);
}
void IKIGAI::AUDIO::AudioManagerAL::setListenerUp(MATH::Vector3f up) {
	audioEngine->set3dListenerUp(up.x, up.y, up.z);
}

void IKIGAI::AUDIO::AudioManagerAL::setSourceVelocity(SoundResource& source, MATH::Vector3f vel) {
	if (!source.id) {
		//problem
	}
	audioEngine->set3dSourceVelocity(source.id.value(), vel.x, vel.y, vel.z);
}
void IKIGAI::AUDIO::AudioManagerAL::setSourceMinMaxDistance(SoundResource& source, float minDistance, float maxDistance) {
	if (!source.id) {
		//problem
	}
	audioEngine->set3dSourceMinMaxDistance(source.id.value(), minDistance, maxDistance);
}
void IKIGAI::AUDIO::AudioManagerAL::setSourceAttenuation(SoundResource& source, unsigned int model, float factor) {
	if (!source.id) {
		//problem
	}
	audioEngine->set3dSourceAttenuation(source.id.value(), model, factor);
}
void IKIGAI::AUDIO::AudioManagerAL::setSourceDopplerFactor(SoundResource& source, float factor) {
	if (!source.id) {
		//problem
	}
	audioEngine->set3dSourceDopplerFactor(source.id.value(), factor);
}


void IKIGAI::AUDIO::AudioManagerAL::pauseAll() {
	audioEngine->setPauseAll(true);
}

void IKIGAI::AUDIO::AudioManagerAL::unpauseAll() {
	audioEngine->setPauseAll(false);
}

void IKIGAI::AUDIO::AudioManagerAL::stopAll() {
	audioEngine->stopAll();
}

void IKIGAI::AUDIO::AudioManagerAL::stop(SoundResource& source) {
	if (!source.id) {
		//problem
	}
	audioEngine->stop(source.id.value());
}

void IKIGAI::AUDIO::AudioManagerAL::pause(SoundResource& source) {
	if (!source.id) {
		//problem
	}
	audioEngine->setPause(source.id.value(), true);
}

void IKIGAI::AUDIO::AudioManagerAL::unpause(SoundResource& source) {
	if (!source.id) {
		//problem
	}
	audioEngine->setPause(source.id.value(), false);
}

void IKIGAI::AUDIO::AudioManagerAL::setVolume(SoundResource& source, float vol) {
	if (!source.id) {
		//problem
	}
	audioEngine->setVolume(source.id.value(), vol);
}
void IKIGAI::AUDIO::AudioManagerAL::setVolume(AudioMixerSource& source, float vol) {
	audioEngine->setVolume(source.bushandle, vol);
}
void IKIGAI::AUDIO::AudioManagerAL::setGlobalVolume(float vol) {
	audioEngine->setGlobalVolume(vol);
}

void IKIGAI::AUDIO::AudioManagerAL::setPan(SoundResource& source, float vol) {
	if (!source.id) {
		//problem
	}
	audioEngine->setPan(source.id.value(), vol);
}

void IKIGAI::AUDIO::AudioManagerAL::setPan(AudioMixerSource& source, float vol) {
	audioEngine->setPan(source.bushandle, vol);
}

void IKIGAI::AUDIO::AudioManagerAL::setFadePan(SoundResource& source, float val, std::chrono::duration<double> time) {
	if (!source.id) {
		//problem
	}
	audioEngine->fadePan(source.id.value(), val, time.count());
}

void IKIGAI::AUDIO::AudioManagerAL::setFade(SoundResource& source, float val, std::chrono::duration<double> time) {
	if (!source.id) {
		//problem
	}
	audioEngine->fadeVolume(source.id.value(), val, time.count());
}

void IKIGAI::AUDIO::AudioManagerAL::setLoop(SoundResource& source, bool isLooped) {
	if (!source.id) {
		//problem
	}
	audioEngine->setLooping(source.id.value(), isLooped);
}

void IKIGAI::AUDIO::AudioManagerAL::play(SoundResource& source) {
	if (source.data.type == SoundType::SOUND) {
		auto& src = sounds[source.data.pathSoundSource];
		if (source.data.is3D) {
			source.id = audioEngine->play3d(src, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, source.data.volume, source.data.state == SoundStatus::PLAY, source.getBusId());
		}
		else {
			source.id = audioEngine->play(src, source.data.volume, source.data.pan, source.data.state != SoundStatus::PLAY, source.getBusId());
		}
	}
	else {
		auto& src = musics[source.data.pathSoundSource];
		if (source.data.is3D) {
			source.id = audioEngine->play3d(src, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, source.data.volume, source.data.state == SoundStatus::PLAY, source.getBusId());
		}
		else {
			source.id = audioEngine->play(src, source.data.volume, source.data.pan, source.data.state != SoundStatus::PLAY, source.getBusId());
		}
	}
}
