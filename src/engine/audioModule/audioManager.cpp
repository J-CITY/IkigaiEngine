#include "audioManager.h"

#include "resourceModule/fileSystem/fileSystem.h"
#include <limits>
#include <stdexcept>
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

namespace IKIGAI::AUDIO {
namespace {
	std::string makeClipKey(const SoundConfig& config, uint64_t generation) {
		return std::to_string(generation) + ":" + RESOURCES::FileSystem::normalizePath(config.pathSoundSource);
	}

	std::shared_ptr<SoLoud::AudioSource> loadClip(const SoundConfig& config) {
		auto& files = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
		auto file = files.getFile(config.pathSoundSource);
		auto bytes = file && file->isValid() ? file->read() : std::vector<uint8_t> {};
		if (bytes.empty() || bytes.size() > std::numeric_limits<unsigned int>::max()) {
			throw std::runtime_error("Missing, empty or oversized audio source: " + config.pathSoundSource);
		}
		const auto size = static_cast<unsigned int>(bytes.size());
		if (config.type == SoundType::SOUND) {
			auto clip = std::make_shared<SoLoud::Wav>();
			if (clip->loadMem(bytes.data(), size, false, false) == SoLoud::SO_NO_ERROR) {
				return clip;
			}
		}
		else {
			auto clip = std::make_shared<SoLoud::WavStream>();
			// Stream voices decode independently from one immutable compressed snapshot.
			if (clip->loadMem(bytes.data(), size, true, false) == SoLoud::SO_NO_ERROR) {
				return clip;
			}
		}
		throw std::runtime_error("Cannot decode audio source: " + config.pathSoundSource);
	}
}

unsigned AudioManagerAL::playBus(SoLoud::Bus& bus) {
	if (!isReady()) {
		return 0;
	}
	std::lock_guard lock(mVoiceMutex);
	return audioEngine->play(bus);
}

AudioManagerAL::ClipCache& AudioManagerAL::clipCache(SoundType type) {
	return type == SoundType::SOUND ? mSounds : mStreams;
}

std::shared_ptr<SoLoud::AudioSource> AudioManagerAL::getClip(const SoundConfig& config) {
	const auto generation = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().generation();
	const auto key = makeClipKey(config, generation);
	auto& cache = clipCache(config.type);
	{
		std::lock_guard lock(mClipMutex);
		if (const auto existing = cache.find(key); existing != cache.end()) {
			if (auto clip = existing->second.lock()) {
				return clip;
			}
		}
	}
	auto clip = loadClip(config);
	std::lock_guard lock(mClipMutex);
	auto& entry = cache[key];
	if (auto existing = entry.lock()) {
		return existing;
	}
	entry = clip;
	return clip;
}

void AudioManagerAL::startVoice(SoundResource& source, bool paused, bool reserveForReload) {
	if (!isReady()) {
		return;
	}
	std::lock_guard lock(mVoiceMutex);
	if (reserveForReload && audioEngine->getVoiceCount() >= VOICE_COUNT) {
		throw std::runtime_error("No spare audio voice for reload: " + source.path);
	}
	auto& clip = *source.clip;
	const auto& config = source.data;
	const auto bus = source.getBusId();
	SoLoud::handle handle = 0;
	if (config.type == SoundType::BACKGROUND) {
		handle = audioEngine->playBackground(clip, config.volume, paused, bus);
	}
	else if (config.is3D) {
		handle = audioEngine->play3d(clip, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, config.volume, paused, bus);
	}
	else {
		handle = audioEngine->play(clip, config.volume, config.pan, paused, bus);
	}
	if (!audioEngine->isValidVoiceHandle(handle)) {
		throw std::runtime_error("Cannot create audio voice: " + source.path);
	}
	source.id = handle;
	audioEngine->setLooping(handle, config.isLooped);
	if (reserveForReload) {
		audioEngine->setProtectVoice(handle, true);
	}
	if (config.is3D && config.type == SoundType::MUSIC) {
		audioEngine->set3dSourceMinMaxDistance(handle, 0.0f, 100.0f);
		audioEngine->set3dSourceAttenuation(handle, SoLoud::AudioSource::LINEAR_DISTANCE, 1.0f);
		audioEngine->update3dAudio();
	}
}

void AudioManagerAL::addSource(SoundResource& source) {
	if (!isReady()) {
		return;
	}
	if (!source.clip) {
		source.clip = getClip(source.data);
	}
	startVoice(source, source.data.state != SoundStatus::PLAY, false);
}

void AudioManagerAL::removeSource(SoundResource& source) {
	stop(source);
	source.id.reset();
	source.clip.reset();
}

AudioManagerAL::PreparedReload AudioManagerAL::prepareReload(const SoundResource& current, const SoundConfig& config, uint64_t generation) {
	PreparedReload result { SoundResource(config.pathSoundSource, config), makeClipKey(config, generation) };
	result.source.mixer = current.mixer;
	result.source.clip = loadClip(config);
	{
		std::lock_guard lock(mClipMutex);
		clipCache(config.type).try_emplace(result.clipKey);
	}
	startVoice(result.source, true, true);
	return result;
}

void AudioManagerAL::finishReload(SoundResource& live, PreparedReload& prepared, bool committed) {
	removeSource(prepared.source);
	if (!committed) {
		return;
	}
	{
		std::lock_guard lock(mClipMutex);
		clipCache(live.data.type).find(prepared.clipKey)->second = live.clip;
	}
	if (live.id) {
		audioEngine->setProtectVoice(*live.id, false);
		if (live.data.state == SoundStatus::PLAY) {
			audioEngine->setPause(*live.id, false);
		}
	}
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

void IKIGAI::AUDIO::AudioManagerAL::setSourcePos(const SoundResource& source, MATH::Vector3f pos) {
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
		// problem
	}
	audioEngine->set3dSourceVelocity(source.id.value(), vel.x, vel.y, vel.z);
}
void IKIGAI::AUDIO::AudioManagerAL::setSourceMinMaxDistance(SoundResource& source, float minDistance, float maxDistance) {
	if (!source.id) {
		// problem
	}
	audioEngine->set3dSourceMinMaxDistance(source.id.value(), minDistance, maxDistance);
}
void IKIGAI::AUDIO::AudioManagerAL::setSourceAttenuation(SoundResource& source, unsigned int model, float factor) {
	if (!source.id) {
		// problem
	}
	audioEngine->set3dSourceAttenuation(source.id.value(), model, factor);
}
void IKIGAI::AUDIO::AudioManagerAL::setSourceDopplerFactor(SoundResource& source, float factor) {
	if (!source.id) {
		// problem
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
	if (isReady() && source.id) {
		audioEngine->stop(*source.id);
	}
}

void IKIGAI::AUDIO::AudioManagerAL::pause(SoundResource& source) {
	if (!source.id) {
		// problem
	}
	audioEngine->setPause(source.id.value(), true);
}

void IKIGAI::AUDIO::AudioManagerAL::unpause(SoundResource& source) {
	if (!source.id) {
		// problem
	}
	audioEngine->setPause(source.id.value(), false);
}

void IKIGAI::AUDIO::AudioManagerAL::setVolume(SoundResource& source, float vol) {
	if (!source.id) {
		// problem
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
		// problem
	}
	audioEngine->setPan(source.id.value(), vol);
}

void IKIGAI::AUDIO::AudioManagerAL::setPan(AudioMixerSource& source, float vol) {
	audioEngine->setPan(source.bushandle, vol);
}

void IKIGAI::AUDIO::AudioManagerAL::setFadePan(SoundResource& source, float val, std::chrono::duration<double> time) {
	if (!source.id) {
		// problem
	}
	audioEngine->fadePan(source.id.value(), val, time.count());
}

void IKIGAI::AUDIO::AudioManagerAL::setFade(SoundResource& source, float val, std::chrono::duration<double> time) {
	if (!source.id) {
		// problem
	}
	audioEngine->fadeVolume(source.id.value(), val, time.count());
}

void IKIGAI::AUDIO::AudioManagerAL::setLoop(SoundResource& source, bool isLooped) {
	if (!source.id) {
		// problem
	}
	audioEngine->setLooping(source.id.value(), isLooped);
}

void IKIGAI::AUDIO::AudioManagerAL::play(SoundResource& source) {
	addSource(source);
}
