#include "audioSourceManager.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/pathGetter.h"
#include <audioModule/audioManager.h>
#include <audioModule/audioSource.h>
#include <resourceModule/fileSystem/fileSystem.h>
#include <taskModule/taskSystem.h>

namespace IKIGAI::RESOURCES {
COROUTINE::Task<ResourcePtr<AUDIO::SoundResource>> AudioSourceLoader::createResourceTask(
	std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	AUDIO::SoundConfig config;
	config.pathSoundSource = path;
	ConfigCandidate candidate;
	if (type == ELoadingType::RESOURCE) {
		candidate = BeginConfigLoad(path);
		if (!candidate.descriptor) {
			auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
			if (!file || !file->isValid()) {
				throw std::runtime_error("Missing audio descriptor: " + path);
			}
			auto bytes = co_await file->readAllTask(control);
			ParseConfigCandidate(candidate, std::string(bytes.begin(), bytes.end()), path);
		}
		config = *candidate.descriptor;
	}
	control->check();
	co_await COROUTINE::NextFrame();
	control->check();
	const auto desiredState = config.state;
	config.state = AUDIO::SoundStatus::STOP;
	auto resource = CreateFromResource(config, nullptr, createCacheDeleter(path));
	resource->data.state = desiredState;
	if (candidate.descriptor && !PublishConfig(path, candidate)) {
		throw ResourceLoadCancelled();
	}
	auto published = registerResource(path, std::move(resource), control.get());
	addFileWatchSubscribe(path, { path, config.pathSoundSource }, published);
	co_return published;
}
using SoundDeleter = std::function<void(AUDIO::SoundResource*)>;

ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::CreateFromFile(const std::string& path, UTILS::IAllocator* allocator, AudioDeleter deleter) {
	AUDIO::SoundConfig config;
	config.pathSoundSource = path;
	config.type = AUDIO::SoundType::SOUND;
	config.volume = -1.0f;
	config.state = AUDIO::SoundStatus::STOP;
	config.pan = 0.0f;
	config.is3D = false;
	config.isLooped = false;

	auto resource = MEMORY::MakeSharedWithDeleter<AUDIO::SoundResource, MEMORY::Category::Audio>(allocator, std::move(deleter), path, config);
	AUDIO::AudioManagerAL::GetInstance().addSource(*resource);
	resource->path = path;

	return resource;
}

ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::CreateFromResource(const std::string& path, UTILS::IAllocator* allocator, AudioDeleter deleter) {
	AUDIO::SoundConfig config = LoadConfig(path);
	return CreateFromResource(config, allocator, deleter);
}

ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::CreateFromResource(
	const AUDIO::SoundConfig& config, UTILS::IAllocator* allocator, AudioDeleter deleter) {
	if (!ServiceManager::Get<FileSystem>().isFileExist(config.pathSoundSource)) {
		throw std::runtime_error("Missing audio source: " + config.pathSoundSource);
	}
	auto resource
		= MEMORY::MakeSharedWithDeleter<AUDIO::SoundResource, MEMORY::Category::Audio>(allocator, std::move(deleter), config.pathSoundSource, config);

	AUDIO::AudioManagerAL::GetInstance().addSource(*resource);
	resource->path = config.pathSoundSource;

	// AddFileWatchSubscribe(path, config.pathSoundSource, resource);
	return resource;
}

ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::createResource(const std::string& path) {
	if (path.ends_with(".sound")) {
		return createResource(path, ELoadingType::RESOURCE);
	}
	return createResource(path, ELoadingType::FILE);
}

ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::createResource(const std::string& path, ELoadingType type) {
	return createResource(path, type, std::any());
}

ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
	ResourcePtr<AUDIO::SoundResource> res;
	std::unordered_set<std::string> paths;
	if (type == ELoadingType::RESOURCE) {
		const auto candidate = ReadConfigCandidate(path);
		const auto& config = *candidate.descriptor;
		paths.insert(config.pathSoundSource);
		res = CreateFromResource(config, nullptr, createCacheDeleter(path));
		if (res && !PublishConfig(path, candidate)) {
			return nullptr;
		}
	}
	else if (type == ELoadingType::FILE) {
		res = CreateFromFile(path, nullptr, createCacheDeleter(path));
	}
	{ // FileWatch
		paths.insert(path);
		addFileWatchSubscribe(path, paths, res);
	}

	return res;
}

bool AudioSourceLoader::reloadResource(std::weak_ptr<AUDIO::SoundResource> weakRes, const std::string& path) {
	if (auto sound = weakRes.lock()) {
		std::unordered_set<std::string> paths = { path };
		auto generation = ServiceManager::Get<FileSystem>().generation();
		auto config = sound->data;
		ConfigCandidate candidate;
		if (path.ends_with(".sound")) {
			candidate = ReadConfigCandidate(path, true);
			config = *candidate.descriptor;
			generation = candidate.generation;
			paths.insert(config.pathSoundSource);
		}
		auto& audio = AUDIO::AudioManagerAL::GetInstance();
		auto replacement = audio.prepareReload(*sound, config, generation);
		const auto swap = [&]() noexcept {
			sound->swapReloadedSource(replacement.source);
			return true;
		};
		const bool committed
			= candidate.descriptor ? commitResourceReload(path, sound, path, candidate, swap) : commitResourceSwap(path, sound, generation, swap);
		audio.finishReload(*sound, replacement, committed);
		if (!committed) {
			return false;
		}
		addFileWatchSubscribe(path, paths, weakRes);
		return true;
	}
	return false;
}
}
