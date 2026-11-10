#include "resourcePackManager.h"

#include "coroutineModule/awaitables.h"
#include "coroutineModule/scheduler.h"
#include "fileSystem/fileSystem.h"
#include "serviceManager.h"
#include "taskModule/taskSystem.h"
#include "textureManager.h"
#include "utilsModule/jsonLoader.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <serdepp/attribute/default.hpp>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace IKIGAI::RESOURCES {
namespace {

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

	ELoadingType resolveMethod(const std::string& method, const std::string& packPath) {
		if (method == "resource") {
			return ELoadingType::RESOURCE;
		}
		if (method == "file") {
			return ELoadingType::FILE;
		}
		if (method == "memory") {
			return ELoadingType::MEMORY;
		}
		throw std::runtime_error("Unknown resource loading method '" + method + "' in pack " + packPath);
	}

	class PackLoadCancelled : public std::runtime_error {
	public:
		explicit PackLoadCancelled(const std::string& path)
			: std::runtime_error("Resource pack load cancelled: " + path) { }
	};

}

struct ResourcePackManager::Impl : std::enable_shared_from_this<Impl> {
	enum class Phase { Loading, Ready, RollingBack, Unloading, Failed };
	enum class Reference { Caller, Dependency };
	enum class LoadKind { Registered, AtlasSprite };

	struct LoaderEntry {
		AsyncLoaderFunc load;
		UnloaderFunc unload;
		int priority = 50;
	};
	using Registry = std::map<std::string, std::shared_ptr<const LoaderEntry>>;

	struct Resource {
		std::string path;
		ELoadingType method = ELoadingType::RESOURCE;
		LoadKind kind = LoadKind::Registered;
		std::shared_ptr<const LoaderEntry> loader;
	};

	struct Manifest {
		std::vector<std::string> packs;
		std::vector<Resource> resources;
	};

	struct Pack;
	struct LoadedResource {
		std::string path;
		std::shared_ptr<const LoaderEntry> loader;
		std::any handle;
		std::shared_ptr<ResourceLoadOperation> operation;
	};
	using AcquiredResource = std::variant<std::shared_ptr<Pack>, LoadedResource>;

	struct Pack {
		std::string path;
		std::thread::id finalizer = std::this_thread::get_id();
		Phase phase = Phase::Loading;
		std::size_t callerReferences = 0;
		std::size_t dependencyReferences = 0;
		std::promise<void> promise;
		std::shared_future<void> completion = promise.get_future().share();
		std::shared_ptr<Pack> waitingFor;
		std::vector<AcquiredResource> resources;

		std::size_t& references(Reference kind) {
			return kind == Reference::Caller ? callerReferences : dependencyReferences;
		}
		bool releasing() const {
			return phase == Phase::RollingBack || phase == Phase::Unloading;
		}
		bool referenced() const {
			return callerReferences != 0 || dependencyReferences != 0;
		}
	};

	struct Attempt {
		std::shared_ptr<Impl> owner;
		std::shared_ptr<Pack> pack;
		std::exception_ptr cancellation;
		std::vector<AcquiredResource> resources;
		bool finished = true;

		~Attempt() {
			if (!finished) {
				owner->finish(*this, cancellation);
			}
		}
	};

	struct Acquisition {
		std::shared_ptr<Pack> pack;
		std::shared_ptr<Attempt> attempt;
	};

	struct DependencyWait {
		Impl& owner;
		std::shared_ptr<Pack> parent;

		~DependencyWait() {
			if (parent) {
				std::lock_guard lock(owner.mutex);
				parent->waitingFor.reset();
			}
		}
	};

	struct LoaderContext {
		Impl& owner;
		std::shared_ptr<Pack> pack;
		LoaderContext* previous = activeLoader;

		LoaderContext(Impl& owner, const std::shared_ptr<Pack>& pack)
			: owner(owner)
			, pack(pack) {
			activeLoader = this;
		}
		~LoaderContext() {
			activeLoader = previous;
		}
	};

	static thread_local LoaderContext* activeLoader;
	mutable std::mutex mutex;
	std::condition_variable released;
	Registry loaders;
	std::map<std::string, std::shared_ptr<Pack>> packs;

	void registerLoader(std::string type, std::shared_ptr<const LoaderEntry> loader);
	Acquisition acquire(const std::string& path, Reference kind, const std::shared_ptr<Pack>& parent, ELoadingPolicy policy);
	void release(const std::shared_ptr<Pack>& pack, Reference kind);
	std::exception_ptr releaseResources(std::vector<AcquiredResource>& resources) noexcept;
	void finish(Attempt& attempt, std::exception_ptr error = {}) noexcept;
	void checkInterest(Attempt& attempt);

	static Manifest prepare(const std::string& path, const Registry& registry);
	Registry snapshotLoaders() const;
	std::shared_future<void> loadResource(Attempt& attempt, const Resource& resource, ELoadingPolicy policy);
	void loadSync(const std::shared_ptr<Attempt>& attempt);
	void schedule(const std::shared_ptr<Attempt>& attempt);
	static COROUTINE::Task<> loadAsync(std::shared_ptr<Attempt> attempt, std::shared_ptr<Manifest> manifest, TASK::BackgroundTicket preparation);
};

thread_local ResourcePackManager::Impl::LoaderContext* ResourcePackManager::Impl::activeLoader = nullptr;

void ResourcePackManager::Impl::registerLoader(std::string type, std::shared_ptr<const LoaderEntry> loader) {
	std::shared_ptr<const LoaderEntry> previous;
	{
		std::lock_guard lock(mutex);
		previous = std::exchange(loaders[std::move(type)], std::move(loader));
	}
}

auto ResourcePackManager::Impl::acquire(const std::string& path, Reference kind, const std::shared_ptr<Pack>& parent, ELoadingPolicy policy)
	-> Acquisition {
	std::unique_lock lock(mutex);
	for (;;) {
		const auto found = packs.find(path);
		if (found == packs.end() || found->second->phase == Phase::Failed) {
			break;
		}

		const auto pack = found->second;
		if (pack->releasing()) {
			if (pack->finalizer == std::this_thread::get_id() || parent) {
				throw std::logic_error("Resource pack is being released: " + path);
			}
			released.wait(lock, [&] { return !pack->releasing(); });
			continue;
		}

		if (pack->phase == Phase::Loading) {
			if (parent) {
				for (auto dependency = pack; dependency; dependency = dependency->waitingFor) {
					if (dependency == parent) {
						throw std::runtime_error("Resource pack dependency cycle: " + parent->path + " -> " + path);
					}
				}
			}
			if (policy != ELoadingPolicy::Asynchronous && pack->finalizer == std::this_thread::get_id()) {
				throw std::logic_error("Cannot synchronously join a pending resource pack on its finalization thread: " + path);
			}
			if (policy != ELoadingPolicy::Asynchronous && TASK::isTaskExecutorCallback()) {
				throw std::logic_error("Task executor callbacks cannot synchronously join a pending resource pack: " + path);
			}
			if (parent) {
				parent->waitingFor = pack;
			}
		}
		++pack->references(kind);
		return { pack, {} };
	}

	auto pack = std::make_shared<Pack>();
	pack->path = path;
	auto attempt = std::make_shared<Attempt>();
	attempt->owner = shared_from_this();
	attempt->pack = pack;
	attempt->cancellation = std::make_exception_ptr(PackLoadCancelled(path));
	packs.insert_or_assign(path, pack);
	++pack->references(kind);
	if (parent) {
		parent->waitingFor = pack;
	}
	attempt->finished = false;
	return { std::move(pack), std::move(attempt) };
}

void ResourcePackManager::Impl::release(const std::shared_ptr<Pack>& pack, Reference kind) {
	std::vector<AcquiredResource> resources;
	{
		std::lock_guard lock(mutex);
		auto& references = pack->references(kind);
		if (references == 0) {
			return;
		}
		--references;
		if (pack->referenced() || pack->phase != Phase::Ready) {
			return;
		}
		pack->phase = Phase::Unloading;
		pack->finalizer = std::this_thread::get_id();
		resources = std::move(pack->resources);
	}

	LoaderContext context(*this, pack);
	const auto error = releaseResources(resources);
	{
		std::lock_guard lock(mutex);
		pack->phase = Phase::Failed;
		packs.erase(pack->path);
	}
	released.notify_all();
	if (error) {
		std::rethrow_exception(error);
	}
}

std::exception_ptr ResourcePackManager::Impl::releaseResources(std::vector<AcquiredResource>& resources) noexcept {
	std::exception_ptr firstError;
	while (!resources.empty()) {
		try {
			if (auto* dependency = std::get_if<std::shared_ptr<Pack>>(&resources.back())) {
				release(*dependency, Reference::Dependency);
			}
			else {
				auto& resource = std::get<LoadedResource>(resources.back());
				if (resource.operation) {
					const auto completion = resource.operation->completion();
					if (!completion.valid() || completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
						resource.operation->cancelAndWait();
					}
				}
				if (resource.loader->unload) {
					resource.loader->unload(resource.path);
				}
			}
		} catch (...) {
			if (!firstError) {
				firstError = std::current_exception();
			}
		}
		resources.pop_back();
	}
	return firstError;
}

void ResourcePackManager::Impl::finish(Attempt& attempt, std::exception_ptr error) noexcept {
	auto& pack = *attempt.pack;
	{
		std::lock_guard lock(mutex);
		if (attempt.finished) {
			return;
		}
		attempt.finished = true;
		pack.waitingFor.reset();
		if (!error && pack.referenced()) {
			pack.resources = std::move(attempt.resources);
			pack.phase = Phase::Ready;
			pack.promise.set_value();
			return;
		}

		pack.phase = Phase::RollingBack;
		pack.finalizer = std::this_thread::get_id();
	}
	LoaderContext context(*this, attempt.pack);
	releaseResources(attempt.resources);
	{
		std::lock_guard lock(mutex);
		pack.phase = Phase::Failed;
		pack.callerReferences = 0;
		pack.dependencyReferences = 0;
		pack.promise.set_exception(error ? error : attempt.cancellation);
	}
	released.notify_all();
}

void ResourcePackManager::Impl::checkInterest(Attempt& attempt) {
	std::lock_guard lock(mutex);
	if (!attempt.pack->referenced()) {
		// Once cancellation is observed, later consumers must start a fresh attempt.
		attempt.pack->phase = Phase::RollingBack;
		std::rethrow_exception(attempt.cancellation);
	}
}

auto ResourcePackManager::Impl::snapshotLoaders() const -> Registry {
	std::lock_guard lock(mutex);
	return loaders;
}

auto ResourcePackManager::Impl::prepare(const std::string& path, const Registry& registry) -> Manifest {
	auto parsed = UTILS::FromJson<ResourcePackDef>(path);
	if (parsed.isErr()) {
		throw std::runtime_error("Cannot read resource pack " + path + ": " + parsed.unwrapErr().text);
	}
	auto definition = parsed.unwrap();

	Manifest result;
	result.packs.reserve(definition.packs.size());
	for (const auto& dependency : definition.packs) {
		result.packs.push_back(FileSystem::normalizePath(dependency));
	}

	for (const auto& [type, items] : definition.resources) {
		const auto loader = registry.find(type);
		if (loader == registry.end()) {
			throw std::runtime_error("Unknown resource type '" + type + "' in pack " + path);
		}
		for (const auto& item : items) {
			if (item.path.empty()) {
				continue;
			}
			const auto kind = !item.atlas.empty() && (type == "texture" || type == "textures") ? LoadKind::AtlasSprite : LoadKind::Registered;
			result.resources.push_back({ item.path, resolveMethod(item.method, path), kind, loader->second });
		}
	}
	std::stable_sort(result.resources.begin(), result.resources.end(),
		[](const auto& left, const auto& right) { return left.loader->priority < right.loader->priority; });
	return result;
}

std::shared_future<void> ResourcePackManager::Impl::loadResource(Attempt& attempt, const Resource& resource, ELoadingPolicy policy) {
	LoadedResource loaded { resource.path, resource.loader, {}, {} };
	ResourceLoadResult result;
	{
		LoaderContext context(*this, attempt.pack);
		if (resource.kind == LoadKind::AtlasSprite) {
			auto texture = ServiceManager::Get<TextureLoader>().createSprite(resource.path);
			if (!texture) {
				throw std::runtime_error("Cannot load sprite in resource pack: " + resource.path);
			}
			result.handle = std::move(texture);
		}
		else {
			result = resource.loader->load(resource.path, resource.method, policy);
		}
	}
	loaded.handle = std::move(result.handle);
	loaded.operation = std::move(result.operation);
	const auto completion = loaded.operation ? loaded.operation->completion() : std::shared_future<void> {};
	attempt.resources.emplace_back(std::move(loaded));
	if (std::get<LoadedResource>(attempt.resources.back()).operation && !completion.valid()) {
		throw std::logic_error("Resource pack loader returned an invalid completion: " + resource.path);
	}
	return completion;
}

void ResourcePackManager::Impl::loadSync(const std::shared_ptr<Attempt>& attempt) {
	try {
		checkInterest(*attempt);
		const auto manifest = prepare(attempt->pack->path, snapshotLoaders());
		attempt->resources.reserve(manifest.packs.size() + manifest.resources.size());
		for (const auto& dependency : manifest.packs) {
			checkInterest(*attempt);
			const auto child = acquire(dependency, Reference::Dependency, attempt->pack, ELoadingPolicy::Synchronous);
			DependencyWait wait { *this, attempt->pack };
			attempt->resources.emplace_back(child.pack);
			if (child.attempt) {
				loadSync(child.attempt);
			}
			child.pack->completion.get();
		}
		for (const auto& resource : manifest.resources) {
			checkInterest(*attempt);
			const auto completion = loadResource(*attempt, resource, ELoadingPolicy::Synchronous);
			if (completion.valid()) {
				if (completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
					throw std::logic_error("Synchronous resource loader returned a pending completion: " + resource.path);
				}
				completion.get();
			}
		}
		finish(*attempt);
	} catch (...) {
		finish(*attempt, std::current_exception());
	}
}

void ResourcePackManager::Impl::schedule(const std::shared_ptr<Attempt>& attempt) {
	try {
		auto& scheduler = ServiceManager::Get<COROUTINE::Scheduler>();
		if (!scheduler.acceptingTasks()) {
			throw std::logic_error("Resource pack requires an active coroutine scheduler");
		}
		auto& tasks = ServiceManager::Get<TASK::TaskSystem>();
		auto manifest = std::make_shared<Manifest>();
		auto ticket
			= tasks.dispatch("Resource pack manifest", [manifest, path = attempt->pack->path, registry = snapshotLoaders()](std::stop_token stop) {
				  if (!stop.stop_requested()) {
					  *manifest = prepare(path, registry);
				  }
			  });
		if (!ticket.accepted()) {
			const auto rejection = ticket.get();
			if (rejection.Exception) {
				std::rethrow_exception(rejection.Exception);
			}
			throw std::runtime_error(std::string("Resource pack dispatch rejected: ") + TASK::backgroundRejectionMessage(rejection.Rejection));
		}
		scheduler.start(loadAsync(attempt, std::move(manifest), std::move(ticket)), "Resource pack: " + attempt->pack->path);
	} catch (...) {
		finish(*attempt, std::current_exception());
	}
}

COROUTINE::Task<> ResourcePackManager::Impl::loadAsync(
	std::shared_ptr<Attempt> attempt, std::shared_ptr<Manifest> manifest, TASK::BackgroundTicket preparation) {
	auto& owner = *attempt->owner;
	try {
		while (!preparation.ready()) {
			owner.checkInterest(*attempt);
			co_await COROUTINE::NextFrame();
		}
		const auto prepared = preparation.get();
		if (prepared.Exception) {
			std::rethrow_exception(prepared.Exception);
		}
		if (prepared.Outcome != TASK::BackgroundOutcome::Succeeded) {
			std::rethrow_exception(attempt->cancellation);
		}
		attempt->resources.reserve(manifest->packs.size() + manifest->resources.size());

		for (const auto& dependency : manifest->packs) {
			owner.checkInterest(*attempt);
			auto child = owner.acquire(dependency, Reference::Dependency, attempt->pack, ELoadingPolicy::Asynchronous);
			DependencyWait wait { owner, attempt->pack };
			attempt->resources.emplace_back(child.pack);
			if (auto childAttempt = std::move(child.attempt)) {
				owner.schedule(childAttempt);
			}
			while (child.pack->completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
				owner.checkInterest(*attempt);
				co_await COROUTINE::NextFrame();
			}
			child.pack->completion.get();
		}
		for (const auto& resource : manifest->resources) {
			owner.checkInterest(*attempt);
			const auto completion = owner.loadResource(*attempt, resource, ELoadingPolicy::Asynchronous);
			if (completion.valid()) {
				for (;;) {
					const auto status = completion.wait_for(std::chrono::seconds(0));
					if (status == std::future_status::ready) {
						break;
					}
					if (status == std::future_status::deferred) {
						throw std::logic_error("Resource pack loader returned a deferred completion: " + resource.path);
					}
					co_await COROUTINE::NextFrame();
				}
				completion.get();
			}
			co_await COROUTINE::YieldIfBudgetExceeded();
		}
		owner.finish(*attempt);
	} catch (...) {
		owner.finish(*attempt, std::current_exception());
	}
}

ResourcePackManager::ResourcePackManager()
	: mImpl(std::make_shared<Impl>()) { }

ResourcePackManager& ResourcePackManager::Instance() {
	static ResourcePackManager instance;
	return instance;
}

void ResourcePackManager::registerResourceType(const std::string& resourceType, LoaderFunc loader, UnloaderFunc unloader, int priority) {
	if (!loader) {
		throw std::invalid_argument("Cannot register an empty resource pack loader: " + resourceType);
	}
	registerAsyncResourceType(
		resourceType,
		[loader = std::move(loader)](const std::string& path, ELoadingType method, ELoadingPolicy) {
			return ResourceLoadResult { loader(path, method, ELoadingPolicy::Synchronous), {} };
		},
		std::move(unloader), priority);
}

void ResourcePackManager::registerAsyncResourceType(const std::string& resourceType, AsyncLoaderFunc loader, UnloaderFunc unloader, int priority) {
	if (!loader) {
		throw std::invalid_argument("Cannot register an empty resource pack loader: " + resourceType);
	}
	mImpl->registerLoader(
		resourceType, std::make_shared<const Impl::LoaderEntry>(Impl::LoaderEntry { std::move(loader), std::move(unloader), priority }));
}

void ResourcePackManager::loadPack(const std::string& inputPath, ELoadingPolicy policy) {
	const auto path = FileSystem::normalizePath(inputPath);
	const auto context = Impl::activeLoader;
	const auto parent = policy != ELoadingPolicy::Asynchronous && context && &context->owner == mImpl.get() ? context->pack : nullptr;
	const auto acquired = mImpl->acquire(path, Impl::Reference::Caller, parent, policy);
	Impl::DependencyWait wait { *mImpl, parent };
	if (policy == ELoadingPolicy::Asynchronous) {
		if (acquired.attempt) {
			mImpl->schedule(acquired.attempt);
		}
	}
	else {
		if (acquired.attempt) {
			mImpl->loadSync(acquired.attempt);
		}
		acquired.pack->completion.get();
	}
}

void ResourcePackManager::unloadPack(const std::string& inputPath) {
	const auto path = FileSystem::normalizePath(inputPath);
	std::shared_ptr<Impl::Pack> pack;
	{
		std::lock_guard lock(mImpl->mutex);
		const auto found = mImpl->packs.find(path);
		if (found != mImpl->packs.end()) {
			pack = found->second;
		}
	}
	if (pack) {
		mImpl->release(pack, Impl::Reference::Caller);
	}
}

ResourcePackLoadStatus ResourcePackManager::getPackLoadStatus(const std::string& inputPath) const {
	const auto path = FileSystem::normalizePath(inputPath);
	std::lock_guard lock(mImpl->mutex);
	const auto found = mImpl->packs.find(path);
	if (found == mImpl->packs.end() || (found->second->phase == Impl::Phase::Failed || found->second->phase == Impl::Phase::Unloading)) {
		return ResourcePackLoadStatus::Failed;
	}
	if (found->second->phase == Impl::Phase::Ready) {
		return ResourcePackLoadStatus::Ready;
	}
	return ResourcePackLoadStatus::Pending;
}

std::shared_future<void> ResourcePackManager::getPackLoadFuture(const std::string& inputPath) const {
	const auto path = FileSystem::normalizePath(inputPath);
	std::lock_guard lock(mImpl->mutex);
	const auto found = mImpl->packs.find(path);
	if (found != mImpl->packs.end() && found->second->phase != Impl::Phase::Unloading) {
		return found->second->completion;
	}
	return {};
}

}
