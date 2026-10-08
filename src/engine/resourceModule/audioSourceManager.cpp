#include "audioSourceManager.h"
#include "serviceManager.h"
#include <taskModule/taskSystem.h>
#include <audioModule/audioManager.h>
#include <audioModule/audioSource.h>
#include "utilsModule/jsonLoader.h"
#include "utilsModule/pathGetter.h"
#include <resourceModule/fileSystem/fileSystem.h>
#include "utilsModule/assertion.h"
#include "fileWatcher.h"

namespace IKIGAI::RESOURCES {
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

		auto resource = AllocateResource<AUDIO::SoundResource>(allocator, std::move(deleter), path, config);
		AUDIO::AudioManagerAL::GetInstance().addSource(*resource);
		resource->path = path;

		return resource;
	}

	ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::CreateFromResource(const std::string& path, UTILS::IAllocator* allocator, AudioDeleter deleter) {
		AUDIO::SoundConfig config = LoadConfig(path);
		return CreateFromResource(config, allocator, deleter);
	}

	ResourcePtr<AUDIO::SoundResource> AudioSourceLoader::CreateFromResource(const AUDIO::SoundConfig& config, UTILS::IAllocator* allocator, AudioDeleter deleter) {
		auto resource = AllocateResource<AUDIO::SoundResource>(allocator, std::move(deleter), config.pathSoundSource, config);

		AUDIO::AudioManagerAL::GetInstance().addSource(*resource);
		resource->path = config.pathSoundSource;

		//AddFileWatchSubscribe(path, config.pathSoundSource, resource);
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
			AUDIO::SoundConfig config;
			if (HasConfig(path)) {
				config = *GetConfig(path);
			}
			else {
				config = LoadConfig(path);
				AddConfigToCache(path, config);
			}
			paths.insert(config.pathSoundSource);
			res = CreateFromResource(config, nullptr, createCacheDeleter(path));
		}
		else if (type == ELoadingType::FILE) {
			res = CreateFromFile(path, nullptr, createCacheDeleter(path));
		}
		{// FileWatch
			paths.insert(path);
			addFileWatchSubscribe(path, paths, res);
		}

		return res;
	}

	bool AudioSourceLoader::reloadResource(std::weak_ptr<AUDIO::SoundResource> weakRes, const std::string& path)
	{
		if (auto sound = weakRes.lock()) {
			std::unordered_set<std::string> paths = { path };
			AUDIO::SoundConfig config;
			if (path.ends_with(".sound")) {
				config = LoadConfig(path);
				AddConfigToCache(path, config);
				paths.insert(config.pathSoundSource);
			}
			else {
				config.path = path;
				config.pathSoundSource = path;
			}
			sound->data = config;

			addFileWatchSubscribe(path, paths, weakRes);
			// Simply reloading via removing and adding source back to trigger reload in audioManagerAL if needed
			AUDIO::AudioManagerAL::GetInstance().removeSource(*sound);
			AUDIO::AudioManagerAL::GetInstance().addSource(*sound);
			return true;
		}
		return false;
	}
}
