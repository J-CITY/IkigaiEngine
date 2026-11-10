#pragma once

#include "resourceConfigCache.h"
#include "resourceRequest.h"
#include <any>
#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "fileSystem/fileSystem.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "utilsModule/assertion.h"
#include "utilsModule/event.h"
#include "utilsModule/idGenerator.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/stringUtils.h"

namespace IKIGAI {
namespace RESOURCES {
	template <typename T> using ResourcePtr = std::shared_ptr<T>;

	enum class ELoadingType {
		RESOURCE, // JSON descriptor based
		FILE, // Raw file based
		MEMORY // Memory/Generated
	};

	enum class ELoadingPolicy { Synchronous, Asynchronous, OnDemand };

	class IResourceManager {
	public:
		virtual ~IResourceManager() = default;
		virtual void unloadResource(const std::string& path) = 0;
	};

	template <class ResourceT, class ConfigT = void>
	class ResourceManager : public IResourceManager, protected ResourceConfigCache<ResourceT, ConfigT> {
		struct WatchSet;

	public:
		using ResourceDeleter = std::function<void(ResourceT*)>;

		virtual ~ResourceManager() override {
			for (const auto& load : mLegacyLoads) {
				load.cancel();
			}
			mRequests.shutdown();
			mLifetime.reset();
		}

		static std::string NormalizePath(std::string path) {
			path = UTILS::ReplaceSubstrings(path, "\\", "/");
			for (const auto prefix : { "assets/engine/", "assets/game/" }) {
				if (const auto pos = path.find(prefix); pos != std::string::npos) {
					path = path.substr(pos + std::char_traits<char>::length(prefix));
				}
			}
			return "/" + std::filesystem::path(path).relative_path().lexically_normal().generic_string();
		}
		ResourceRequest<ResourceT> requestResource(std::string path, ELoadingType type = ELoadingType::RESOURCE) {
			path = NormalizePath(std::move(path));
			auto& scheduler = ServiceManager::Get<COROUTINE::Scheduler>();
			auto& files = ServiceManager::Get<FileSystem>();
			const auto generation = files.generation();
			const auto epoch = requestEpoch(path);
			auto key = std::to_string(generation) + ":" + std::to_string(epoch->id) + ":" + std::to_string(static_cast<int>(type)) + ":" + path;
			return mRequests.request(scheduler, std::move(key), [this, path, type, valid = requestValidity(files, generation, epoch)](auto control) {
				control->valid = valid;
				return createResourceTask(path, type, std::move(control));
			});
		}
		COROUTINE::Task<ResourcePtr<ResourceT>> loadResourceTask(std::string path, ELoadingType type = ELoadingType::RESOURCE) {
			auto request = requestResource(std::move(path), type);
			co_return co_await request.wait();
		}

		ResourcePtr<ResourceT> loadResource(const std::string& path) {
			return loadResource(path, ELoadingType::RESOURCE);
		}

		ResourcePtr<ResourceT> loadResource(const std::string& path, ELoadingType type) {
			return loadResource(path, type, std::any());
		}

		ResourcePtr<ResourceT> loadResource(const std::string& path, ELoadingType type, std::any data) {
			return loadResourceChecked(path, type, std::move(data), nullptr);
		}

	protected:
		ResourcePtr<ResourceT> loadResourceChecked(const std::string& path, ELoadingType type, std::any data, const ResourceLoadControl* control) {
			const auto _path = NormalizePath(path);
			const auto creationKey = std::to_string(static_cast<int>(type)) + ":" + _path;
			static thread_local std::unordered_set<std::string> creating;
			struct CycleGuard {
				std::unordered_set<std::string>& set;
				std::string key;
				bool active = false;
				~CycleGuard() {
					if (active) {
						set.erase(key);
					}
				}
			} cycle { creating, creationKey };

			// One normalized path has a single creator. Other threads wait on that
			// attempt without holding mMutex. Same-thread reentry uses the cycle set
			// and must not wait on its own promise.
			std::promise<ResourcePtr<ResourceT>> promise;
			std::shared_future<ResourcePtr<ResourceT>> pending;
			uint64_t attempt = 0;
			{
				std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
				if (control) {
					control->check();
				}
				if (auto resource = findNormalized(_path)) {
					return resource;
				}
				const auto self = std::this_thread::get_id();
				if (creating.contains(creationKey)) {
					throw std::runtime_error("Resource dependency cycle: " + _path);
				}
				if (const auto inFlight = mCache->inFlight.find(_path); inFlight != mCache->inFlight.end()) {
					if (inFlight->second.creator == self) {
						throw std::runtime_error("Resource dependency cycle: " + _path);
					}
					pending = inFlight->second.future;
				}
				else {
					creating.insert(creationKey);
					cycle.active = true;
					pending = promise.get_future().share();
					attempt = ++mCache->nextAttempt;
					mCache->inFlight.emplace(_path, InFlightLoad { pending, self, attempt });
				}
			}
			if (!attempt) {
				return pending.get();
			}

			InFlightGuard slot(this, _path, attempt);
			try {
				auto created = createResource(_path, type, data);
				ResourcePtr<ResourceT> result;
				if (created) {
					std::lock_guard lock(mCache->mutex);
					syncGeneration();
					if (control) {
						control->check();
					}
					const auto active = mCache->inFlight.find(_path);
					if (active != mCache->inFlight.end() && active->second.attempt == attempt) {
						if (const auto existing = mCache->resources.find(_path); existing != mCache->resources.end()) {
							result = existing->second.lock();
						}
						if (!result) {
							result = storeNormalized(_path, std::move(created));
						}
					}
				}
				promise.set_value(result);
				return result;
			} catch (...) {
				try {
					promise.set_exception(std::current_exception());
				} catch (...) { }
				throw;
			}
		}

	public:
		// The deprecated futures are fulfilled by the owner scheduler. Tick it before waiting on a future.
		[[deprecated("Use requestResource or loadResourceTask on the scheduler owner")]]
		std::future<ResourcePtr<ResourceT>> loadResourceAsync(const std::string& path, ELoadingType type) {
			return scheduleAsyncLoad(path, type, std::any());
		}

		[[deprecated("Use requestResource or loadResourceTask on the scheduler owner")]]
		std::future<ResourcePtr<ResourceT>> loadResourceAsync(const std::string& path, ELoadingType type, std::any data) {
			return scheduleAsyncLoad(path, type, std::move(data));
		}

		void unloadResource(const std::string& path) override {
			const auto key = NormalizePath(path);
			{
				std::lock_guard lock(mCache->mutex);
				if (const auto request = mCache->requestEpochs.find(key); request != mCache->requestEpochs.end()) {
					if (const auto epoch = request->second.lock()) {
						epoch->invalidated.store(true);
					}
					mCache->requestEpochs.erase(request);
				}
				mCache->resources.erase(key);
				mCache->inFlight.erase(key);
				mCache->watches.erase(key);
				this->RemoveConfigFromCache(key);
			}
		}

		std::unordered_map<std::string, std::weak_ptr<ResourceT>> getResources() const {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			syncGeneration();
			return mCache->resources;
		}

		virtual ResourcePtr<ResourceT> createResource(const std::string& p_path) = 0;

		virtual ResourcePtr<ResourceT> createResource(const std::string& p_path, ELoadingType type) {
			return createResource(p_path, type, std::any());
		}

		virtual ResourcePtr<ResourceT> createResource(const std::string& p_path, ELoadingType type, std::any data) {
			return createResource(p_path);
		}

		static void SetAssetPaths(const std::string& projectAssetsPath, const std::string& engineAssetsPath) {
			std::lock_guard lock(sAssetPathsMutex);
			PROJECT_ASSETS_PATH = projectAssetsPath;
			ENGINE_ASSETS_PATH = engineAssetsPath;
		}

	protected:
		virtual COROUTINE::Task<ResourcePtr<ResourceT>> createResourceTask(
			std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
			control->check();
			co_await COROUTINE::YieldIfBudgetExceeded();
			control->check();
			// Default loaders may call render or audio dependencies. Each loader owns its CPU preparation boundary.
			co_return loadResourceChecked(path, type, {}, control.get());
		}
		ResourceRequests<ResourceT> mRequests;
		ResourcePtr<ResourceT> getResource(const std::string& path) {
			return findNormalized(NormalizePath(path));
		}

		ResourcePtr<ResourceT> registerResource(
			const std::string& path, std::shared_ptr<ResourceT> res, const ResourceLoadControl* control = nullptr) {
			const auto key = NormalizePath(path);
			for (auto* load = sActiveLoad; load; load = load->previous) {
				if (load->manager == this && load->key == key) {
					// The synchronous load owns final publication and validates its attempt.
					return res;
				}
			}
			return storeNormalized(key, std::move(res), control);
		}

		// Pin mount generation first. Resource identity and config use the same order as unloadResource.
		template <class Candidate, class Swap>
		bool commitResourceReload(const std::string& resourceKey, const ResourcePtr<ResourceT>& resource, const std::string& configKey,
			const Candidate& candidate, Swap swapPreparedState) {
			const auto mount = ServiceManager::Get<FileSystem>().lockGeneration(candidate.generation);
			if (!mount.owns_lock()) {
				return false;
			}
			std::lock_guard lock(mCache->mutex);
			return matchesTrackedResource(resourceKey, resource, candidate.generation) && this->CommitReload(configKey, candidate, swapPreparedState);
		}

		template <class Swap>
			requires std::is_nothrow_invocable_r_v<bool, Swap>
		bool commitResourceSwap(const std::string& key, const ResourcePtr<ResourceT>& resource, uint64_t generation, Swap swapPreparedState) {
			const auto mount = ServiceManager::Get<FileSystem>().lockGeneration(generation);
			if (!mount.owns_lock()) {
				return false;
			}
			std::lock_guard lock(mCache->mutex);
			return matchesTrackedResource(key, resource, generation) && swapPreparedState();
		}

		virtual void addFileWatchSubscribe(
			const std::string& mainPath, const std::unordered_set<std::string>& watchPaths, std::weak_ptr<ResourceT> weakRes) {
			watchResourceFiles(mainPath, watchPaths, std::move(weakRes), mainPath);
		}

		void watchResourceFiles(const std::string& mainPath, const std::unordered_set<std::string>& watchPaths, std::weak_ptr<ResourceT> weakRes,
			const std::string& configPath) {
#ifdef USE_FILE_WATCHER
			setFileWatches(mainPath, watchPaths, std::move(weakRes), configPath, false);
#else
			(void)mainPath;
			(void)watchPaths;
			(void)weakRes;
			(void)configPath;
#endif
		}

	private:
#ifdef USE_FILE_WATCHER
		void setFileWatches(const std::string& mainPath, const std::unordered_set<std::string>& watchPaths, std::weak_ptr<ResourceT> weakRes,
			const std::string& configPath, bool failed, const std::shared_ptr<WatchSet>& expected = {}) {
			if (weakRes.expired()) {
				return;
			}
			const auto key = NormalizePath(mainPath);
			const auto generation = ServiceManager::Get<FileSystem>().generation();
			std::shared_ptr<WatchSet> previous;
			{
				std::lock_guard lock(mCache->mutex);
				std::erase_if(mCache->watches,
					[generation](const auto& entry) { return entry.second->generation != generation || entry.second->resource.expired(); });
				if (const auto existing = mCache->watches.find(key); existing != mCache->watches.end()) {
					previous = existing->second;
				}
			}
			if (expected && previous != expected) {
				return;
			}
			auto updated = std::make_shared<WatchSet>();
			updated->generation = generation;
			updated->resource = weakRes;
			updated->sourcePaths = watchPaths;
			updated->configPath = NormalizePath(configPath);
			auto dependencies = watchPaths;
			if (const auto parents = this->ConfigParents(updated->configPath)) {
				dependencies.insert(parents->begin(), parents->end());
			}
			if (failed && previous) {
				if (const auto attempted = this->ConfigParents(updated->configPath, true)) {
					dependencies.insert(attempted->begin(), attempted->end());
				}
				for (const auto& [dependency, registration] : previous->registrations) {
					dependencies.insert(dependency);
				}
			}
			const bool sameResource = previous && !previous->resource.owner_before(weakRes) && !weakRes.owner_before(previous->resource);
			updated->group = sameResource ? previous->group : std::make_shared<int>(0);
			const auto callback
				= [this, key, generation, weakRes, group = updated->group, lifetime = std::weak_ptr<int>(mLifetime)](FileWatcher::FileStatus) {
					  // Application-thread dispatch and manager teardown share the same owner.
					  const auto resource = weakRes.lock();
					  if (!resource || lifetime.expired() || ServiceManager::Get<FileSystem>().generation() != generation) {
						  return;
					  }
					  {
						  std::lock_guard lock(mCache->mutex);
						  const auto active = mCache->watches.find(key);
						  const auto current = mCache->resources.find(key);
						  if (active == mCache->watches.end() || active->second->group != group || current == mCache->resources.end()
							  || current->second.owner_before(weakRes) || weakRes.owner_before(current->second)) {
							  return;
						  }
					  }
					  const auto refresh = [&](bool failed) {
						  std::shared_ptr<WatchSet> watched;
						  {
							  std::lock_guard lock(mCache->mutex);
							  const auto current = mCache->watches.find(key);
							  if (current == mCache->watches.end() || current->second->group != group
								  || !matchesTrackedResource(key, resource, generation)) {
								  return;
							  }
							  watched = current->second;
						  }
						  setFileWatches(key, watched->sourcePaths, resource, watched->configPath, failed, watched);
					  };
					  bool succeeded = false;
					  try {
						  succeeded = reloadResource(resource, key);
					  } catch (...) {
						  refresh(true);
						  throw;
					  }
					  refresh(!succeeded);
				  };
			for (const auto& path : dependencies) {
				try {
					const auto dependency = NormalizePath(path);
					if (updated->registrations.contains(dependency)) {
						continue;
					}
					if (sameResource) {
						if (const auto existing = previous->registrations.find(dependency); existing != previous->registrations.end()) {
							updated->registrations.emplace(dependency, existing->second);
							continue;
						}
					}
					auto registration = std::make_shared<WatchRegistration>();
					registration->path = dependency;
					registration->id = FileWatcher::getInstance()->addResourceWatch(dependency, callback, updated->group, weakRes);
					if (registration->id.valid()) {
						updated->registrations.emplace(dependency, std::move(registration));
					}
				} catch (const std::exception& error) {
					std::cerr << "Cannot watch resource dependency " << path << ": " << error.what() << '\n';
				}
			}
			std::lock_guard lock(mCache->mutex);
			const auto existing = mCache->watches.find(key);
			const auto active = mCache->inFlight.find(key);
			if ((existing == mCache->watches.end() ? nullptr : existing->second) == previous
				&& ServiceManager::Get<FileSystem>().generation() == generation
				&& (!sActiveLoad || sActiveLoad->manager != this || sActiveLoad->key != key
					|| (active != mCache->inFlight.end() && active->second.attempt == sActiveLoad->attempt))) {
				mCache->watches.insert_or_assign(key, std::move(updated));
			}
		}
#endif

	protected:
		virtual void unsubscribeFileWatch(const std::string& mainPath) {
#ifndef USE_FILE_WATCHER
			(void)mainPath;
#else
			std::lock_guard lock(mCache->mutex);
			mCache->watches.erase(NormalizePath(mainPath));
#endif
		}

		virtual bool reloadResource(std::weak_ptr<ResourceT> weakRes, const std::string& config) = 0;

		virtual ResourceDeleter createCacheDeleter(const std::string& path) {
			const auto key = NormalizePath(path);
			return [key, lifetime = std::weak_ptr<int>(mLifetime), cache = std::weak_ptr<Cache>(mCache)](ResourceT* ptr) {
				// Hold both tokens for the whole erase. expired() then using the manager was a race.
				const auto alive = lifetime.lock();
				const auto state = cache.lock();
				if (!alive || !state) {
					return;
				}
				std::lock_guard<std::recursive_mutex> lock(state->mutex);
				const auto it = state->resources.find(key);
				if (it == state->resources.end()) {
					return;
				}
				const auto live = it->second.lock();
				if (live && live.get() != ptr) {
					return;
				}
				state->resources.erase(it);
			};
		}

	private:
		bool matchesTrackedResource(const std::string& key, const ResourcePtr<ResourceT>& resource, uint64_t generation) const {
			const auto current = mCache->resources.find(key);
			return mCache->generation == generation && current != mCache->resources.end() && !current->second.owner_before(resource)
				&& !resource.owner_before(current->second);
		}

		struct RequestEpoch {
			uint64_t id = 0;
			std::atomic_bool invalidated = false;
		};

		std::shared_ptr<RequestEpoch> requestEpoch(const std::string& path) {
			std::lock_guard lock(mCache->mutex);
			std::erase_if(mCache->requestEpochs, [](const auto& entry) { return entry.second.expired(); });
			auto& entry = mCache->requestEpochs[path];
			if (auto epoch = entry.lock()) {
				return epoch;
			}
			auto epoch = std::make_shared<RequestEpoch>();
			epoch->id = ++mCache->nextRequestEpoch;
			entry = epoch;
			return epoch;
		}

		static auto requestValidity(FileSystem& files, uint64_t generation, std::shared_ptr<RequestEpoch> epoch) {
			return [&files, generation, epoch = std::move(epoch)] { return !epoch->invalidated.load() && files.generation() == generation; };
		}

		struct WatchRegistration {
			std::string path;
			EVENT::Event<FileWatcher::FileStatus>::ID id;

			~WatchRegistration() {
				if (id.valid()) {
					FileWatcher::getInstance()->remove(path, id);
				}
			}
		};

		struct WatchSet {
			uint64_t generation = 0;
			std::weak_ptr<ResourceT> resource;
			std::shared_ptr<int> group;
			std::unordered_set<std::string> sourcePaths;
			std::string configPath;
			std::unordered_map<std::string, std::shared_ptr<WatchRegistration>> registrations;
		};

		struct InFlightLoad {
			std::shared_future<ResourcePtr<ResourceT>> future;
			std::thread::id creator;
			uint64_t attempt = 0;
		};

		struct InFlightGuard {
			InFlightGuard(ResourceManager* manager, const std::string& key, uint64_t attempt)
				: manager(manager)
				, key(key)
				, attempt(attempt)
				, previous(std::exchange(sActiveLoad, this)) { }
			~InFlightGuard() {
				sActiveLoad = previous;
				manager->clearInFlight(key, attempt);
			}
			InFlightGuard(const InFlightGuard&) = delete;
			InFlightGuard& operator=(const InFlightGuard&) = delete;

			ResourceManager* manager;
			const std::string& key;
			uint64_t attempt;
			InFlightGuard* previous;
		};
		inline static thread_local InFlightGuard* sActiveLoad = nullptr;

		ResourcePtr<ResourceT> findNormalized(const std::string& path) {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			syncGeneration();
			if (const auto resource = mCache->resources.find(path); resource != mCache->resources.end()) {
				return resource->second.lock();
			}
			return nullptr;
		}

		void syncGeneration() const {
			const auto generation = ServiceManager::Get<FileSystem>().generation();
			if (mCache->generation != generation) {
				mCache->resources.clear();
				mCache->inFlight.clear();
				mCache->watches.clear();
				mCache->generation = generation;
			}
		}

		ResourcePtr<ResourceT> storeNormalized(
			const std::string& path, std::shared_ptr<ResourceT> resource, const ResourceLoadControl* control = nullptr) {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			syncGeneration();
			if (control) {
				control->check();
			}
			mCache->resources[path] = resource;
			return resource;
		}

		void clearInFlight(const std::string& path, uint64_t attempt) {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			if (const auto active = mCache->inFlight.find(path); active != mCache->inFlight.end() && active->second.attempt == attempt) {
				mCache->inFlight.erase(active);
			}
		}

		struct LegacyLoadCompletion {
			std::promise<ResourcePtr<ResourceT>> promise;
			std::exception_ptr cancellation = std::make_exception_ptr(ResourceLoadCancelled {});
			bool settled = false;

			~LegacyLoadCompletion() {
				if (!settled) {
					promise.set_exception(cancellation);
				}
			}

			void setValue(ResourcePtr<ResourceT> resource) {
				promise.set_value(std::move(resource));
				settled = true;
			}

			void setError(std::exception_ptr error) {
				promise.set_exception(std::move(error));
				settled = true;
			}
		};

		static COROUTINE::Task<> resolveLegacyRequest(ResourceRequest<ResourceT> request, std::shared_ptr<LegacyLoadCompletion> completion) {
			try {
				completion->setValue(co_await request.wait());
			} catch (...) {
				completion->setError(std::current_exception());
			}
		}

		COROUTINE::Task<> resolveLegacyLoad(
			std::string path, ELoadingType type, std::any data, ResourceLoadControl control, std::shared_ptr<LegacyLoadCompletion> completion) {
			try {
				co_await COROUTINE::YieldIfBudgetExceeded();
				auto resource = loadResourceChecked(path, type, std::move(data), &control);
				control.check();
				completion->setValue(std::move(resource));
			} catch (...) {
				completion->setError(std::current_exception());
			}
		}

		std::future<ResourcePtr<ResourceT>> scheduleAsyncLoad(const std::string& path, ELoadingType type, std::any data) {
			auto completion = std::make_shared<LegacyLoadCompletion>();
			auto future = completion->promise.get_future();
			try {
				auto& scheduler = ServiceManager::Get<COROUTINE::Scheduler>();
				if (!scheduler.acceptingTasks()) {
					throw ResourceLoadCancelled();
				}
				std::erase_if(mLegacyLoads, [](const auto& load) { return load.done(); });
				mLegacyLoads.emplace_back();
				try {
					COROUTINE::Task<> load;
					if (data.has_value()) {
						auto normalized = NormalizePath(path);
						auto& files = ServiceManager::Get<FileSystem>();
						ResourceLoadControl control;
						control.valid = requestValidity(files, files.generation(), requestEpoch(normalized));
						load = resolveLegacyLoad(std::move(normalized), type, std::move(data), std::move(control), completion);
					}
					else {
						load = resolveLegacyRequest(requestResource(path, type), completion);
					}
					mLegacyLoads.back() = scheduler.start(std::move(load), "Resource: " + path);
				} catch (...) {
					mLegacyLoads.pop_back();
					throw;
				}
			} catch (...) {
				completion->setError(std::current_exception());
			}
			return future;
		}

		struct Cache {
			mutable std::recursive_mutex mutex;
			std::unordered_map<std::string, std::weak_ptr<ResourceT>> resources;
			std::unordered_map<std::string, InFlightLoad> inFlight;
			std::unordered_map<std::string, std::weak_ptr<RequestEpoch>> requestEpochs;
			std::unordered_map<std::string, std::shared_ptr<WatchSet>> watches;
			uint64_t generation = ~uint64_t { 0 };
			uint64_t nextAttempt = 0;
			uint64_t nextRequestEpoch = 0;
		};

		std::shared_ptr<Cache> mCache = std::make_shared<Cache>();
		std::shared_ptr<int> mLifetime = std::make_shared<int>(0);
		std::vector<COROUTINE::TaskHandle> mLegacyLoads;
		inline static std::string PROJECT_ASSETS_PATH = "";
		inline static std::string ENGINE_ASSETS_PATH = "";
		inline static std::mutex sAssetPathsMutex;
	};
};
};
