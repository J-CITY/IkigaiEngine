#include "audioManager.h"

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
	constexpr std::size_t MaxCachedClipsPerType = 256;

	std::string makeClipKey(const ClipByteSource& source, const SoundConfig& config, uint64_t generation) {
		return std::to_string(generation) + ":" + source.normalizePath(config.pathSoundSource);
	}

	std::shared_ptr<SoLoud::AudioSource> loadClip(const ClipByteSource& source, const SoundConfig& config) {
		auto bytes = source.read(config.pathSoundSource);
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

AudioManagerAL::ClipCache::iterator AudioManagerAL::reserveClipCacheEntry(ClipCache& cache, const std::string& key) {
	if (const auto existing = cache.find(key); existing != cache.end()) {
		return existing;
	}
	if (cache.size() >= MaxCachedClipsPerType) {
		std::erase_if(cache, [](const auto& entry) { return entry.second.expired(); });
	}
	// The resource owns the decoded clip even if all cache entries are in use.
	return cache.size() < MaxCachedClipsPerType ? cache.try_emplace(key).first : cache.end();
}

std::shared_ptr<SoLoud::AudioSource> AudioManagerAL::getClip(const SoundConfig& config) {
	const auto& source = UTILS::ServiceManager::Get<const ClipByteSource>();
	const auto key = makeClipKey(source, config, source.generation());
	auto& cache = clipCache(config.type);
	{
		std::lock_guard lock(mClipMutex);
		if (const auto existing = cache.find(key); existing != cache.end()) {
			if (auto clip = existing->second.lock()) {
				return clip;
			}
		}
	}
	auto clip = loadClip(source, config);
	std::lock_guard lock(mClipMutex);
	if (const auto entry = reserveClipCacheEntry(cache, key); entry != cache.end()) {
		if (auto existing = entry->second.lock()) {
			return existing;
		}
		entry->second = clip;
	}
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
	const auto& source = UTILS::ServiceManager::Get<const ClipByteSource>();
	PreparedReload result { SoundResource(config.pathSoundSource, config), makeClipKey(source, config, generation) };
	result.source.mixer = current.mixer;
	result.source.clip = loadClip(source, config);
	{
		std::lock_guard lock(mClipMutex);
		reserveClipCacheEntry(clipCache(config.type), result.clipKey);
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
		auto& cache = clipCache(live.data.type);
		// The cache may have been full or reclaimed this entry while the reload was prepared.
		if (const auto entry = cache.find(prepared.clipKey); entry != cache.end()) {
			entry->second = live.clip;
		}
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
