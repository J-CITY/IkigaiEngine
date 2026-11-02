#pragma once

#include "resourceConfigCache.h"
#include "resourceRequest.h"
#include <any>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "fileSystem/fileSystem.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "taskModule/taskSystem.h"
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
	public:
		using ResourceDeleter = std::function<void(ResourceT*)>;

		virtual ~ResourceManager() override {
			mRequests.shutdown();
			mLifetime.reset();
		}

		static std::string NormalizePath(std::string path) {
			if (!path.empty() && path.front() == '/' && path.find('\\') == std::string::npos && path.find("assets/engine/") == std::string::npos
				&& path.find("assets/game/") == std::string::npos) {
				return path;
			}
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
			auto key = std::to_string(generation) + ":" + std::to_string(static_cast<int>(type)) + ":" + path;
			return mRequests.request(scheduler, std::move(key), [this, path, type, generation, &files](auto control) {
				control->valid = [&files, generation] { return files.generation() == generation; };
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
			bool creator = false;
			{
				std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
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
					mCache->inFlight.emplace(_path, InFlightLoad { pending, self });
					creator = true;
				}
			}
			if (!creator) {
				return pending.get();
			}

			InFlightGuard slot(this, _path);
			try {
				auto created = createResource(_path, type, data);
				ResourcePtr<ResourceT> result;
				if (created) {
					result = storeNormalized(_path, std::move(created));
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

		[[deprecated("Use requestResource or loadResourceTask on the main thread")]]
		std::future<ResourcePtr<ResourceT>> loadResourceAsync(const std::string& path, ELoadingType type) {
			return scheduleAsyncLoad(path, type, std::any());
		}

		[[deprecated("Use requestResource or loadResourceTask on the main thread")]]
		std::future<ResourcePtr<ResourceT>> loadResourceAsync(const std::string& path, ELoadingType type, std::any data) {
			return scheduleAsyncLoad(path, type, std::move(data));
		}

		void unloadResource(const std::string& path) override {
			const auto key = NormalizePath(path);
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			mCache->resources.erase(key);
		}

		std::unordered_map<std::string, std::weak_ptr<ResourceT>> getResources() const {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
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
			PROJECT_ASSETS_PATH = projectAssetsPath;
			ENGINE_ASSETS_PATH = engineAssetsPath;
		}

	protected:
		virtual COROUTINE::Task<ResourcePtr<ResourceT>> createResourceTask(
			std::string path, ELoadingType type, std::shared_ptr<ResourceLoadControl> control) {
			control->check();
			co_await COROUTINE::YieldIfBudgetExceeded();
			control->check();
			// Loaders that create GPU objects override this and stay on the coroutine thread.
			// The default path is file/CPU work: run it on the pool and yield the scheduler.
			if (ServiceManager::Check<IKIGAI::TASK::TaskSystem>()) {
				auto& tasks = ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
				if (tasks.isReady()) {
					auto promise = std::make_shared<std::promise<ResourcePtr<ResourceT>>>();
					auto future = promise->get_future();
					auto handle = tasks.submit("createResourceTask", -1, nullptr, [this, path, type, promise]() {
						try {
							promise->set_value(this->loadResource(path, type));
						} catch (...) {
							promise->set_exception(std::current_exception());
						}
					});
					if (handle.mTask) {
						handle.mTask->continueWith([handle]() mutable { handle = {}; });
						while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
							control->check();
							co_await COROUTINE::NextFrame();
						}
						co_return future.get();
					}
				}
			}
			co_return loadResource(path, type);
		}
		ResourceRequests<ResourceT> mRequests;
		ResourcePtr<ResourceT> getResource(const std::string& path) {
			return findNormalized(NormalizePath(path));
		}

		ResourcePtr<ResourceT> registerResource(const std::string& path, std::shared_ptr<ResourceT> res) {
			return storeNormalized(NormalizePath(path), std::move(res));
		}

		virtual void addFileWatchSubscribe(
			const std::string& mainPath, const std::unordered_set<std::string>& watchPaths, std::weak_ptr<ResourceT> weakRes) {
#ifndef USE_FILE_WATCHER
			(void)mainPath;
			(void)watchPaths;
			(void)weakRes;
			return;
#endif
			auto fwCb = [this, mainPath, watchPaths, weakRes, lifetime = std::weak_ptr<int>(mLifetime)](RESOURCES::FileWatcher::FileStatus status) {
				const auto alive = lifetime.lock();
				if (!alive) {
					return;
				}
				if (status == RESOURCES::FileWatcher::FileStatus::MODIFIED) {
					if (weakRes.lock()) {
						// A rejected edit keeps the existing subscriptions and their observed
						// timestamps. Re-registering here could swallow the next valid save.
						if (reloadResource(weakRes, mainPath)) {
							unsubscribeFileWatch(mainPath);
							addFileWatchSubscribe(mainPath, watchPaths, weakRes);
						}
					}
				}
			};

			for (const auto& path : watchPaths) {
				auto saveCb = [mainPath, path](auto e) { sFWSubscribersIds[mainPath].emplace_back(path, e); };
				RESOURCES::FileWatcher::getInstance()->addDeferred(path, fwCb, saveCb);
			}
		}

		virtual void unsubscribeFileWatch(const std::string& mainPath) {
#ifndef USE_FILE_WATCHER
			(void)mainPath;
			return;
#endif
			for (auto& e : sFWSubscribersIds[mainPath]) {
				RESOURCES::FileWatcher::getInstance()->removeDeferred(e.first, e.second);
			}
			sFWSubscribersIds.erase(mainPath);
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

		// FW ids
		inline static std::unordered_map<std::string, std::vector<std::pair<std::string, IdGenerator<EVENT::Event<>>::ID>>> sFWSubscribersIds;

	private:
		struct InFlightLoad {
			std::shared_future<ResourcePtr<ResourceT>> future;
			std::thread::id creator;
		};

		class InFlightGuard {
		public:
			InFlightGuard(ResourceManager* manager, std::string key)
				: mManager(manager)
				, mKey(std::move(key)) { }
			~InFlightGuard() {
				if (mManager) {
					mManager->clearInFlight(mKey);
				}
			}
			InFlightGuard(const InFlightGuard&) = delete;
			InFlightGuard& operator=(const InFlightGuard&) = delete;

		private:
			ResourceManager* mManager = nullptr;
			std::string mKey;
		};

		ResourcePtr<ResourceT> findNormalized(const std::string& path) {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			const auto generation = ServiceManager::Get<FileSystem>().generation();
			if (mCache->generation != generation) {
				mCache->resources.clear();
				this->ClearConfigCache();
				mCache->generation = generation;
			}
			if (const auto resource = mCache->resources.find(path); resource != mCache->resources.end()) {
				return resource->second.lock();
			}
			return nullptr;
		}

		ResourcePtr<ResourceT> storeNormalized(const std::string& path, std::shared_ptr<ResourceT> resource) {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			mCache->resources[path] = resource;
			return resource;
		}

		void clearInFlight(const std::string& path) {
			std::lock_guard<std::recursive_mutex> lock(mCache->mutex);
			mCache->inFlight.erase(path);
		}

		std::future<ResourcePtr<ResourceT>> scheduleAsyncLoad(const std::string& path, ELoadingType type, std::any data) {
			if (ServiceManager::Check<IKIGAI::TASK::TaskSystem>()) {
				auto& tasks = ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
				if (tasks.isReady()) {
					auto promise = std::make_shared<std::promise<ResourcePtr<ResourceT>>>();
					auto future = promise->get_future();
					auto handle = tasks.submit("loadResourceAsync", -1, nullptr, [this, path, type, data, promise]() {
						try {
							promise->set_value(this->loadResource(path, type, data));
						} catch (...) {
							promise->set_exception(std::current_exception());
						}
					});
					// Keep the task handle alive until the pool task finishes. Dropping it
					// here would wait inside Future's destructor and make this call synchronous.
					if (handle.mTask) {
						handle.mTask->continueWith([handle]() mutable { handle = {}; });
						return future;
					}
				}
			}
			return std::async(std::launch::async, [this, path, type, data]() { return this->loadResource(path, type, data); });
		}

		struct Cache {
			mutable std::recursive_mutex mutex;
			std::unordered_map<std::string, std::weak_ptr<ResourceT>> resources;
			std::unordered_map<std::string, InFlightLoad> inFlight;
			uint64_t generation = ~uint64_t { 0 };
		};

		std::shared_ptr<Cache> mCache = std::make_shared<Cache>();
		std::shared_ptr<int> mLifetime = std::make_shared<int>(0);
		inline static std::string PROJECT_ASSETS_PATH = "";
		inline static std::string ENGINE_ASSETS_PATH = "";
	};
};
};
