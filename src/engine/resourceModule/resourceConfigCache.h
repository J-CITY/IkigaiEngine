#pragma once

#include "utilsModule/jsonLoader.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace IKIGAI::RESOURCES {
using ConfigParentPaths = std::shared_ptr<const std::vector<std::string>>;

// Keep descriptor caches isolated by resource type, including shared descriptor types.
template <class ResourceT, class ConfigT> class ResourceConfigCache {
protected:
	struct ConfigCandidate {
		std::shared_ptr<const ConfigT> descriptor;
		ConfigParentPaths parents;
		uint64_t generation = 0;
		uint64_t epoch = 0;
		uint64_t revision = 0;
	};

	static ConfigT LoadConfig(const std::string& path) {
		return CheckConfig(UTILS::FromJson<ConfigT>(path), path);
	}

	static ConfigT CheckConfig(const UTILS::Result<ConfigT, UTILS::JsonError>& result, const std::string& path) {
		if (result.isErr()) {
			throw std::runtime_error("Resource descriptor " + path + ": " + result.unwrapErr().text);
		}
		auto config = result.unwrap();
		config.path = path;
		return config;
	}

	static ConfigCandidate BeginConfigLoad(const std::string& path, bool reload = false) {
		const auto& files = ServiceManager::Get<FileSystem>();
		std::lock_guard lock(sConfigMutex);
		const auto generation = files.generation();
		if (sVfsGeneration != generation) {
			sResourceCache.clear();
			sVfsGeneration = generation;
			++sConfigEpoch;
		}
		auto& entry = sResourceCache[path];
		if (reload || !entry.descriptor) {
			entry.revision = ++sNextRevision;
		}
		return { reload ? nullptr : entry.descriptor, reload ? nullptr : entry.parents, generation, sConfigEpoch, entry.revision };
	}

	static ConfigCandidate ReadConfigCandidate(const std::string& path, bool reload = false) {
		auto candidate = BeginConfigLoad(path, reload);
		if (!candidate.descriptor) {
			std::vector<std::string> parents;
			auto result = UTILS::FromJson<ConfigT>(path, &parents);
			if (!parents.empty()) {
				candidate.parents = std::make_shared<const std::vector<std::string>>(std::move(parents));
			}
			if (reload) {
				std::lock_guard lock(sConfigMutex);
				if (auto* entry = FindCurrentConfig(path, candidate)) {
					entry->attemptedParents = candidate.parents;
				}
			}
			candidate.descriptor = std::make_shared<const ConfigT>(CheckConfig(result, path));
		}
		return candidate;
	}

	static void ParseConfigCandidate(ConfigCandidate& candidate, const std::string& text, const std::string& path) {
		std::vector<std::string> parents;
		const auto result = UTILS::FromJsonStr<ConfigT>(text, path, &parents);
		candidate.descriptor = std::make_shared<const ConfigT>(CheckConfig(result, path));
		if (!parents.empty()) {
			candidate.parents = std::make_shared<const std::vector<std::string>>(std::move(parents));
		}
	}

	static ConfigParentPaths ConfigParents(const std::string& path, bool attempted = false) {
		std::lock_guard lock(sConfigMutex);
		const auto entry = sResourceCache.find(path);
		if (entry == sResourceCache.end() || sVfsGeneration != ServiceManager::Get<FileSystem>().generation()) {
			return {};
		}
		return attempted ? entry->second.attemptedParents : entry->second.parents;
	}

	static bool IsCurrentConfig(const std::string& path, const ConfigCandidate& candidate) {
		std::lock_guard lock(sConfigMutex);
		return FindCurrentConfig(path, candidate) != nullptr;
	}

	static bool PublishConfig(const std::string& path, const ConfigCandidate& candidate, bool reload = false) {
		const auto generation = ServiceManager::Get<FileSystem>().lockGeneration(candidate.generation);
		if (!generation.owns_lock()) {
			return false;
		}
		std::lock_guard lock(sConfigMutex);
		auto* entry = FindCurrentConfig(path, candidate);
		if (!entry) {
			return false;
		}
		if (!reload && entry->descriptor) {
			return entry->descriptor == candidate.descriptor;
		}
		if (candidate.descriptor) {
			entry->descriptor = candidate.descriptor;
			entry->parents = candidate.parents;
			entry->attemptedParents.reset();
		}
		return bool(candidate.descriptor);
	}

	// Keep an owner-side state swap and its descriptor publication together.
	// The swap only moves prepared data and must not run I/O or call listeners.
	template <class Swap>
		requires std::is_nothrow_invocable_v<Swap>
	static bool CommitReload(const std::string& path, const ConfigCandidate& candidate, Swap swapPreparedState) {
		const auto generation = ServiceManager::Get<FileSystem>().lockGeneration(candidate.generation);
		if (!generation.owns_lock()) {
			return false;
		}
		std::lock_guard lock(sConfigMutex);
		auto* entry = FindCurrentConfig(path, candidate);
		if (!entry || !candidate.descriptor) {
			return false;
		}
		if constexpr (std::is_same_v<std::invoke_result_t<Swap>, bool>) {
			if (!swapPreparedState()) {
				return false;
			}
		}
		else {
			swapPreparedState();
		}
		entry->descriptor = candidate.descriptor;
		entry->parents = candidate.parents;
		entry->attemptedParents.reset();
		return true;
	}

	static void RemoveConfigFromCache(const std::string& path) {
		std::lock_guard lock(sConfigMutex);
		sResourceCache.erase(path);
	}

	static void ClearConfigCache() {
		std::lock_guard lock(sConfigMutex);
		sResourceCache.clear();
		++sConfigEpoch;
	}

private:
	struct ConfigEntry {
		std::shared_ptr<const ConfigT> descriptor;
		ConfigParentPaths parents;
		ConfigParentPaths attemptedParents;
		uint64_t revision = 0;
	};

	static ConfigEntry* FindCurrentConfig(const std::string& path, const ConfigCandidate& candidate) {
		if (candidate.epoch != sConfigEpoch || candidate.generation != ServiceManager::Get<FileSystem>().generation()) {
			return nullptr;
		}
		const auto entry = sResourceCache.find(path);
		return entry != sResourceCache.end() && entry->second.revision == candidate.revision ? &entry->second : nullptr;
	}

	inline static std::mutex sConfigMutex;
	inline static uint64_t sConfigEpoch = 0;
	inline static uint64_t sVfsGeneration = ~uint64_t { 0 };
	inline static uint64_t sNextRevision = 0;
	inline static std::unordered_map<std::string, ConfigEntry> sResourceCache;
};

// Compiling loaders can own their schema without a second serialized descriptor.
template <class ResourceT> class ResourceConfigCache<ResourceT, void> {
protected:
	static ConfigParentPaths ConfigParents(const std::string&, bool = false) {
		return {};
	}
	static void RemoveConfigFromCache(const std::string&) { }
	static void ClearConfigCache() { }
};
}
