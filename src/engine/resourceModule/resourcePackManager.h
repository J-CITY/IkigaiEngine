#pragma once

#include "resourceManager.h"
#include <any>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace IKIGAI::RESOURCES {

enum class ResourcePackLoadStatus { Pending, Ready, Failed };

class ResourcePackManager {
public:
	using LoaderFunc = std::function<std::any(const std::string& path, ELoadingType type, ELoadingPolicy policy)>;
	using UnloaderFunc = std::function<void(const std::string& path)>;

	class ResourceLoadOperation {
	public:
		virtual ~ResourceLoadOperation() = default;
		// The same valid future on every call. Its readiness also means the operation has stopped accessing the handle.
		[[nodiscard]] virtual std::shared_future<void> completion() const noexcept = 0;
		// Quiesce on the loader owner thread without requiring another scheduler tick.
		virtual void cancelAndWait() noexcept = 0;
	};
	struct ResourceLoadResult {
		std::any handle;
		std::shared_ptr<ResourceLoadOperation> operation;
	};
	using AsyncLoaderFunc = std::function<ResourceLoadResult(const std::string& path, ELoadingType type, ELoadingPolicy policy)>;

	ResourcePackManager();
	ResourcePackManager(const ResourcePackManager&) = delete;
	ResourcePackManager& operator=(const ResourcePackManager&) = delete;
	static ResourcePackManager& Instance();

	// Legacy loaders receive Synchronous and must return a fully loaded handle or throw. Lower priorities run first.
	void registerResourceType(const std::string& resourceType, LoaderFunc loader, UnloaderFunc unloader, int priority = 50);
	// An operation must settle before dependents can load. For a synchronous call it must already be ready.
	void registerAsyncResourceType(const std::string& resourceType, AsyncLoaderFunc loader, UnloaderFunc unloader, int priority = 50);

	// New asynchronous loads start on the coroutine scheduler owner. Synchronous loads and unload callbacks
	// run on the caller, who must respect the registered loaders' thread affinity.
	// Each call acquires a reference. A pending attempt with no references rolls back after any active resource settles.
	// Synchronous joins on the finalization thread or from task executor callbacks throw while the pack is pending.
	void loadPack(const std::string& path, ELoadingPolicy policy = ELoadingPolicy::Synchronous);
	void unloadPack(const std::string& path);
	[[nodiscard]] ResourcePackLoadStatus getPackLoadStatus(const std::string& path) const;
	[[nodiscard]] std::shared_future<void> getPackLoadFuture(const std::string& path) const;

private:
	struct Impl;
	std::shared_ptr<Impl> mImpl;
};
}
