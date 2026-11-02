#pragma once
#include "resourceManager.h"
#include <algorithm>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include <any>
#include <set>

namespace IKIGAI::RESOURCES {

enum class ResourcePackLoadStatus { Pending, Ready, Failed };

struct ResourcePackLoadState {
	ResourcePackLoadStatus status = ResourcePackLoadStatus::Pending;
	std::shared_future<void> completion;
	std::exception_ptr error;
};

class ResourcePackManager {
public:
	using LoaderFunc = std::function<std::any(const std::string& path, ELoadingType type, ELoadingPolicy policy)>;
	using UnloaderFunc = std::function<void(const std::string& path)>;

	struct ResourceLoaderEntry {
		LoaderFunc loader;
		UnloaderFunc unloader;
		/// Lower value = loaded first.
		/// Recommended defaults:
		///   skeleton=0, animation=10, blendspace=20,
		///   model/texture/shader/material/audio=50
		int priority = 50;
	};

	static ResourcePackManager& Instance() {
		static ResourcePackManager instance;
		return instance;
	}

	void registerResourceType(const std::string& resourceType, LoaderFunc loader, UnloaderFunc unloader, int priority = 50) {
		mLoaders[resourceType] = ResourceLoaderEntry { std::move(loader), std::move(unloader), priority };
	}

	void loadPack(const std::string& path, ELoadingPolicy policy = ELoadingPolicy::Synchronous);
	void unloadPack(const std::string& path);

	[[nodiscard]] ResourcePackLoadStatus getPackLoadStatus(const std::string& path) const;
	[[nodiscard]] std::shared_future<void> getPackLoadFuture(const std::string& path) const;

private:
	// Returns an error message when the pack could not be loaded.
	std::optional<std::string> loadPackBody(const std::string& path, ELoadingPolicy policy);
	void finishPackLoadFailed(const std::string& path, const std::shared_ptr<ResourcePackLoadState>& state, const std::string& message);

	std::map<std::string, ResourceLoaderEntry> mLoaders;
	std::map<std::string, UnloaderFunc> mUnloaders;

	struct LoadedResource {
		std::string type;
		std::string path;
		std::any handle;
	};
	// Map PackPath -> List of loaded resources
	std::map<std::string, std::vector<LoadedResource>> mLoadedPacks;
	std::map<std::string, size_t> mReferences;
	std::set<std::string> mLoading;

	std::map<std::string, std::shared_ptr<ResourcePackLoadState>> mPackLoadStates;
	mutable std::mutex mPackLoadStatesMutex;

	std::vector<std::any> mAsyncFutures;
};
}
