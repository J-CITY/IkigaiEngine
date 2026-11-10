
#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
// #include <irrKlang.h>

#include <unordered_set>

#include "audioMixerSource.h"
#include "audioSource.h"
#include "mathModule/math.h"
#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"
#include "utilsModule/singleton.h"


namespace IKIGAI {
namespace AUDIO {
	struct Sound2d {
		std::string path;
	};

	struct Sound3d : public Sound2d {
		MATH::Vector3f pos;
	};

	class AudioManager {
		// irrklang::ISoundEngine* soundEngine;
	public:
		AudioManager() {
			// soundEngine = irrklang::createIrrKlangDevice();
		}

		~AudioManager() {
			// soundEngine->drop();
		}

		void play2d(Sound2d sound, bool isLooped) {
			// auto soundTracker = soundEngine->play2D(sound.path.c_str(), isLooped);
		}

		void play3d(Sound3d sound, bool isLooped) {
			// auto soundTracker = soundEngine->play3D(sound.path.c_str(), irrklang::vec3df(sound.pos.x, sound.pos.y, sound.pos.z), isLooped);
		}

		void setListenerPos(MATH::Vector3f pos, MATH::Vector3f dir) {
			// soundEngine->setListenerPosition(irrklang::vec3df(pos.x, pos.y, pos.z), irrklang::vec3df(dir.x, dir.y, dir.z));
		}

		void pauseAll() { }

		void unpauseAll() { }

		void pause() { }

		void unpause() { }
	};

	// TODO:
	// Add component whitch call audio system

	class AudioManagerAL : public UTILS::SingletonService<AudioManagerAL> {
		FRIEND_SINGLETON_SERVICE(AudioManagerAL)

		std::unique_ptr<SoLoud::Soloud> audioEngine;
		bool mReady = false;

		using ClipCache = std::unordered_map<std::string, std::weak_ptr<SoLoud::AudioSource>>;
		ClipCache mSounds;
		ClipCache mStreams;
		std::mutex mClipMutex;
		std::mutex mVoiceMutex;
		ClipCache& clipCache(SoundType type);
		std::shared_ptr<SoLoud::AudioSource> getClip(const SoundConfig& config);
		void startVoice(SoundResource& source, bool paused, bool reserveForReload);
		AudioManagerAL();

		bool isReady() const {
			return mReady && audioEngine;
		}

	public:
		struct PreparedReload {
			SoundResource source;
			std::string clipKey;
		};

		~AudioManagerAL() override;

		unsigned int playBus(SoLoud::Bus& bus);

		void addSource(SoundResource& source);
		void removeSource(SoundResource& source);
		PreparedReload prepareReload(const SoundResource& current, const SoundConfig& config, uint64_t generation);
		void finishReload(SoundResource& live, PreparedReload& prepared, bool committed);

		void set3dSoundSpeedSpeed(float speed);
		void setListenerPos(MATH::Vector3f pos, MATH::Vector3f dir);

		void setListenerVelocity(MATH::Vector3f vel);
		void setListenerUp(MATH::Vector3f up);
		void setSourcePos(const SoundResource& source, MATH::Vector3f pos);
		void setSourceVelocity(SoundResource& source, MATH::Vector3f vel);
		void setSourceMinMaxDistance(SoundResource& source, float minDistance, float maxDistance);
		void setSourceAttenuation(SoundResource& source, unsigned int model, float factor);
		void setSourceDopplerFactor(SoundResource& source, float factor);

		void pauseAll();
		void unpauseAll();

		void stopAll();
		void stop(SoundResource& source);
		void pause(SoundResource& source);
		void play(SoundResource& source);
		void unpause(SoundResource& source);
		void setVolume(SoundResource& source, float vol);
		void setVolume(AudioMixerSource& source, float vol);
		void setGlobalVolume(float vol);
		void setPan(SoundResource& source, float vol);
		void setPan(AudioMixerSource& source, float vol);
		void setFadePan(SoundResource& source, float val, std::chrono::duration<double> time);
		void setFade(SoundResource& source, float val, std::chrono::duration<double> time);
		void setLoop(SoundResource& source, bool isLooped);
	};
}
};
