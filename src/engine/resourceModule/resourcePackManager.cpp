#include "resourcePackManager.h"
#include "serviceManager.h"
#include "textureManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/pathGetter.h"
#include <future>
#include <iostream>
#include <resourceModule/fileSystem/fileSystem.h>
#include <serdepp/attribute/default.hpp>
#include <string>
#include <taskModule/taskSystem.h>
#include <vector>

namespace IKIGAI::RESOURCES {

struct ResourcePackItem {
	std::string path;
	std::string method = "resource";
	std::string atlas;

	template <class Context> constexpr static auto serde(Context& context, ResourcePackItem& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&ResourcePackItem::path, "path")
			.field(&ResourcePackItem::method, "method", default_ { "resource" })
			.field(&ResourcePackItem::atlas, "atlas", default_ { "" });
	}
};

struct ResourcePackDef {
	std::vector<std::string> packs;
	std::map<std::string, std::vector<ResourcePackItem>> resources;

	template <class Context> constexpr static auto serde(Context& context, ResourcePackDef& value) {
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&ResourcePackDef::packs, "packs", default_ { std::vector<std::string>() })
			.field(&ResourcePackDef::resources, "resources", default_ { std::map<std::string, std::vector<ResourcePackItem>>() });
	}
};

ResourcePackLoadStatus ResourcePackManager::getPackLoadStatus(const std::string& inputPath) const {
	const auto path = FileSystem::normalizePath(inputPath);
	if (mLoadedPacks.contains(path)) {
		return ResourcePackLoadStatus::Ready;
	}
	std::lock_guard<std::mutex> lock { mPackLoadStatesMutex };
	if (const auto it = mPackLoadStates.find(path); it != mPackLoadStates.end()) {
		return it->second->status;
	}
	return ResourcePackLoadStatus::Failed;
}

std::shared_future<void> ResourcePackManager::getPackLoadFuture(const std::string& inputPath) const {
	const auto path = FileSystem::normalizePath(inputPath);
	if (mLoadedPacks.contains(path)) {
		std::promise<void> ready;
		ready.set_value();
		return ready.get_future().share();
	}
	std::lock_guard<std::mutex> lock { mPackLoadStatesMutex };
	if (const auto it = mPackLoadStates.find(path); it != mPackLoadStates.end()) {
		return it->second->completion;
	}
	return {};
}

void ResourcePackManager::finishPackLoadFailed(
	const std::string& path, const std::shared_ptr<ResourcePackLoadState>& state, const std::string& message) {
	LOG_ERROR << message;
	state->status = ResourcePackLoadStatus::Failed;
	state->error = std::make_exception_ptr(std::runtime_error(message));
	mLoading.erase(path);
	{
		std::lock_guard<std::mutex> lock { mPackLoadStatesMutex };
		mPackLoadStates[path] = state;
	}
}

void ResourcePackManager::loadPack(const std::string& inputPath, ELoadingPolicy policy) {
	const auto path = FileSystem::normalizePath(inputPath);
	if (mLoadedPacks.contains(path)) {
		++mReferences[path];
		return;
	}
	if (!mLoading.insert(path).second) {
		throw std::runtime_error("Resource pack cycle: " + path);
	}

	if (policy == ELoadingPolicy::Asynchronous) {
		auto state = std::make_shared<ResourcePackLoadState>();
		auto promise = std::make_shared<std::promise<void>>();
		state->completion = promise->get_future().share();
		state->status = ResourcePackLoadStatus::Pending;
		{
			std::lock_guard<std::mutex> lock { mPackLoadStatesMutex };
			mPackLoadStates[path] = state;
		}

		const auto complete = [state, promise](ResourcePackLoadStatus status, std::exception_ptr error = {}) {
			state->status = status;
			state->error = std::move(error);
			if (status == ResourcePackLoadStatus::Ready) {
				promise->set_value();
			}
			else {
				try {
					if (error) {
						std::rethrow_exception(error);
					}
					throw std::runtime_error("Resource pack load failed");
				} catch (...) {
					promise->set_exception(std::current_exception());
				}
			}
		};

		const auto runOnWorker = [this, path, policy, state, complete]() {
			struct Guard {
				std::set<std::string>& loading;
				std::string path;
				~Guard() {
					loading.erase(path);
				}
			} guard { mLoading, path };
			try {
				if (const auto error = loadPackBody(path, policy)) {
					LOG_ERROR << *error;
					complete(ResourcePackLoadStatus::Failed, std::make_exception_ptr(std::runtime_error(*error)));
					return;
				}
				complete(ResourcePackLoadStatus::Ready);
			} catch (...) {
				const auto error = std::current_exception();
				try {
					std::rethrow_exception(error);
				} catch (const std::exception& ex) {
					LOG_ERROR << ("ResourcePackManager: async load failed for " + path + ": " + ex.what());
				} catch (...) {
					LOG_ERROR << ("ResourcePackManager: async load failed for " + path);
				}
				complete(ResourcePackLoadStatus::Failed, error);
			}
		};

		if (ServiceManager::Check<IKIGAI::TASK::TaskSystem>()) {
			auto& tasks = ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
			if (tasks.isReady()) {
				auto handle = tasks.submit("ResourcePackManager::loadPack", -1, nullptr, runOnWorker);
				if (handle.mTask) {
					handle.mTask->continueWith([handle]() mutable { handle = {}; });
					return;
				}
			}
		}

		finishPackLoadFailed(path, state, "ResourcePackManager: asynchronous pack load requires a ready TaskSystem");
		try {
			promise->set_exception(std::make_exception_ptr(std::runtime_error("ResourcePackManager: TaskSystem unavailable")));
		} catch (...) { }
		return;
	}

	struct Guard {
		std::set<std::string>& loading;
		std::string path;
		~Guard() {
			loading.erase(path);
		}
	} guard { mLoading, path };

	if (const auto error = loadPackBody(path, policy)) {
		ASSERT(error->c_str());
	}
}

std::optional<std::string> ResourcePackManager::loadPackBody(const std::string& path, ELoadingPolicy policy) {
	if (!IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::RESOURCES::FileSystem>().isFileExist(path)) {
		return "ResourcePackManager: Failed to load pack: " + path;
	}

	auto res = UTILS::FromJson<ResourcePackDef>(path);
	if (res.isErr()) {
		return "ResourcePackManager: JSON Parse Error in " + path + ": " + res.unwrapErr().text;
	}
	auto packDef = res.unwrap();

	std::vector<LoadedResource> loadedResources;

	for (const auto& pPath : packDef.packs) {
		// Nested packs keep dependency order on the loading thread; only the top-level call may be offloaded.
		loadPack(pPath, policy == ELoadingPolicy::Asynchronous ? ELoadingPolicy::Synchronous : policy);
		loadedResources.push_back({ "pack", pPath });
	}

	// Sort resource sections by loader priority (lower = earlier)
	using SortItem = std::tuple<int, std::string, std::vector<ResourcePackItem>>;
	std::vector<SortItem> sorted;
	sorted.reserve(packDef.resources.size());
	for (const auto& [type, items] : packDef.resources) {
		int prio = 999;
		if (auto it = mLoaders.find(type); it != mLoaders.end()) {
			prio = it->second.priority;
		}
		sorted.emplace_back(prio, type, items);
	}
	std::sort(sorted.begin(), sorted.end(), [](const SortItem& a, const SortItem& b) { return std::get<0>(a) < std::get<0>(b); });

	for (const auto& [prio, type, items] : sorted) {
		if (!mLoaders.contains(type)) {
			ASSERT(std::string("ResourcePackManager: Unknown resource type: " + type).c_str());
			continue;
		}
		auto& entry = mLoaders[type];

		for (const auto& item : items) {
			if (item.path.empty()) {
				continue;
			}

			ELoadingType method = ELoadingType::RESOURCE;
			if (item.method == "file") {
				method = ELoadingType::FILE;
			}
			else if (item.method == "memory") {
				method = ELoadingType::MEMORY;
			}

			auto resLoad = !item.atlas.empty() && (type == "texture" || type == "textures")
				? std::any(ServiceManager::Get<TextureLoader>().createSprite(item.path))
				: entry.loader(item.path, method, policy);
			if (policy == ELoadingPolicy::Asynchronous && resLoad.has_value()) {
				mAsyncFutures.push_back(std::move(resLoad));
			}
			loadedResources.push_back({ type, item.path, std::move(resLoad) });
		}
	}

	mLoadedPacks[path] = std::move(loadedResources);
	mReferences[path] = 1;
	return std::nullopt;
}

void ResourcePackManager::unloadPack(const std::string& inputPath) {
	const auto path = FileSystem::normalizePath(inputPath);
	if (!mLoadedPacks.contains(path)) {
		return;
	}
	if (--mReferences[path] > 0) {
		return;
	}
	mReferences.erase(path);

	const auto& resources = mLoadedPacks[path];
	// Unload in reverse order of loading
	for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
		if (it->type == "pack") {
			unloadPack(it->path);
		}
		else {
			if (auto loaderIt = mLoaders.find(it->type); loaderIt != mLoaders.end()) {
				loaderIt->second.unloader(it->path);
			}
		}
	}
	mLoadedPacks.erase(path);
}

}
