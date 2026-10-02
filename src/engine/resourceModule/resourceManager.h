#pragma once

#include <mutex>
#include <future>
#include <any>
#include <unordered_set>

#include "fileWatcher.h"
#include "serviceManager.h"
#include "fileSystem/fileSystem.h"
#include "utilsModule/assertion.h"
#include "utilsModule/event.h"
#include "utilsModule/idGenerator.h"
#include "utilsModule/jsonLoader.h"
#include "utilsModule/stringUtils.h"

namespace IKIGAI {
	namespace RESOURCES {
		template<typename T>
		using ResourcePtr = std::shared_ptr<T>;

		enum class ELoadingType {
			RESOURCE,   // JSON descriptor based
			FILE,       // Raw file based
			MEMORY      // Memory/Generated
		};

		enum class ELoadingPolicy {
			Synchronous,
			Asynchronous,
			OnDemand
		};

		class IResourceManager {
		public:
			virtual ~IResourceManager() = default;
			virtual void unloadResource(const std::string& path) = 0;
		};

		template<class ResourceT, class ConfigT>
		class ResourceManager : public IResourceManager {
		public:

			using ResourceDeleter = std::function<void(ResourceT*)>;

			virtual ~ResourceManager() override = default;

			ResourcePtr<ResourceT> loadResource(const std::string& path) {
				return loadResource(path, ELoadingType::RESOURCE);
			}

			ResourcePtr<ResourceT> loadResource(const std::string& path, ELoadingType type) {
				return loadResource(path, type, std::any());
			}

			ResourcePtr<ResourceT> loadResource(const std::string& path, ELoadingType type, std::any data) {
				std::string _path = UTILS::ReplaceSubstrings(path, "\\", "/");
				auto pos = _path.find("assets/engine/");
				if (pos != std::string::npos) {
					_path = _path.substr(pos + 14);
				}
				pos = _path.find("assets/game/");
				if (pos != std::string::npos) {
					_path = _path.substr(pos + 12);
				}

				{
					std::lock_guard<std::recursive_mutex> lock(mMutex);
					if (auto resource = getResource(_path)) {
						return resource;
					}
				}

				auto newResource = createResource(_path, type, data);
				if (newResource) {
					return registerResource(_path, newResource);
				}
				return nullptr;
			}

			std::future<ResourcePtr<ResourceT>> loadResourceAsync(const std::string& path, ELoadingType type) {
				//TODO: use task system or corutines
				return std::async(std::launch::async, [this, path, type]() {
					return this->loadResource(path, type);
				});
			}

			std::future<ResourcePtr<ResourceT>> loadResourceAsync(const std::string& path, ELoadingType type, std::any data) {
				//TODO: use task system or corutines
				return std::async(std::launch::async, [this, path, type, data]() {
					return this->loadResource(path, type, data);
				});
			}

			void unloadResource(const std::string& path) override {
				std::lock_guard<std::recursive_mutex> lock(mMutex);
				resources.erase(path);
			}

			std::unordered_map<std::string, std::weak_ptr<ResourceT>>& getResources() {
				return resources;
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
			ResourcePtr<ResourceT> getResource(const std::string& path) {
				// Assumes lock is held by caller if called internally, 
				// but let's use recursive_mutex so we can lock again just in case
				std::lock_guard<std::recursive_mutex> lock(mMutex);
				if (auto resource = resources.find(path); resource != resources.end()) {
					return resource->second.lock();
				}
				return nullptr;
			}

			ResourcePtr<ResourceT> registerResource(const std::string& path, std::shared_ptr<ResourceT> res) {
				std::lock_guard<std::recursive_mutex> lock(mMutex);
				resources[path] = res;
				return res;
			}

			//config cache
			static ConfigT LoadConfig(const std::string& path) {
				auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
				if (!file || !file->isValid()) {
					ASSERT("TextureLoader: cannot open .res file");
				}
				auto parseResult = UTILS::FromJsonStr<ConfigT>(file->readStr());
				if (parseResult.isErr()) {
					ASSERT("TextureLoader: failed to parse .res file");
				}
				auto res = parseResult.unwrap();
				res.path = path;
				return res;
			}

			static bool HasConfig(const std::string& path)
			{
				return sResourceCache.contains(path);
			}

			static std::optional<ConfigT> GetConfig(const std::string& path)
			{
				if (auto it = sResourceCache.find(path); it != sResourceCache.end()) {
					return it->second;
				}
				return std::nullopt;
			}

			static void AddConfigToCache(const std::string& path, const ConfigT& config) {
				sResourceCache[path] = config;
			}

			static void RemoveConfigFromCache(const std::string& path) {
				sResourceCache.erase(path);
			}

			virtual void addFileWatchSubscribe(const std::string& mainPath, const std::unordered_set<std::string>& watchPaths, std::weak_ptr<ResourceT> weakRes) {
#ifdef __EMSCRIPTEN__
				(void)mainPath;
				(void)watchPaths;
				(void)weakRes;
				return;
#endif
				auto fwCb = [this, mainPath, watchPaths, weakRes](RESOURCES::FileWatcher::FileStatus status) {
					if (status == RESOURCES::FileWatcher::FileStatus::MODIFIED) {
						if (weakRes.lock()) {
							unsubscribeFileWatch(mainPath);
							reloadResource(weakRes, mainPath);
							addFileWatchSubscribe(mainPath, watchPaths, weakRes);
						}
					}
				};

				for (const auto& path : watchPaths) {
					auto saveCb = [mainPath, path](auto e) {
						sFWSubscribersIds[mainPath].emplace_back(path, e);
					};
					RESOURCES::FileWatcher::getInstance()->addDeferred(path, fwCb, saveCb);
				}
			}

			virtual void unsubscribeFileWatch(const std::string& mainPath) {
				for (auto& e : sFWSubscribersIds[mainPath]) {
					RESOURCES::FileWatcher::getInstance()->removeDeferred(e.first, e.second);
				}
				sFWSubscribersIds.erase(mainPath);
			}

			virtual bool reloadResource(std::weak_ptr<ResourceT> weakRes, const std::string& config) = 0;





			virtual ResourceDeleter createCacheDeleter(const std::string& path) {
				return [this, path](ResourceT* ptr) {
					this->unloadResource(path);
				};
			}

			mutable std::recursive_mutex mMutex;

			// Кэш разобранных дескрипторов
			inline static std::unordered_map<std::string, ConfigT> sResourceCache;
			// FW ids
			inline static std::unordered_map<std::string, std::vector<std::pair<std::string, IdGenerator<EVENT::Event<>>::ID>>> sFWSubscribersIds;

		private:
			inline static std::string PROJECT_ASSETS_PATH = "";
			inline static std::string ENGINE_ASSETS_PATH = "";

			std::unordered_map<std::string, std::weak_ptr<ResourceT>> resources;
		};
	};
};
