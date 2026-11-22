#pragma once
#include "access.h"
#include "commandBuffer.h"
#include "componentManager.h"
#include "entityManager.h"
#include "signature.h"
#include "systemBatchExecutor.h"
#include <array>
#include <bitset>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IKIGAI::ECS {

class World;
template <typename... Components> class MultiPhaseSystem;

enum class SystemPhase {
	Awake,
	Start,
	SceneStart,
	BeginFrame,
	ObjectFixedUpdate,
	FixedUpdate,
	Physics3D,
	Physics2D,
	FinishFixedUpdate,
	Update,
	BeforeObjectUpdate,
	ObjectUpdate,
	AfterObjectUpdate,
	Present,
	ObjectLateUpdate,
	LateUpdate,
	Destroy,
	Legacy,
	Count,
};

struct SystemPhaseContext {
	virtual ~SystemPhaseContext() = default;
};

class System {
public:
	System();
	virtual ~System() = default;

	// Lifecycle methods
	virtual void onAwake(World& world) { }
	virtual void onStart(World& world) { }
	virtual void onDestroy(World& world) { }

	virtual void onEnable(World& world) { }
	virtual void onDisable(World& world) { }

	// Update phases with delta time
	virtual void onUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) { }
	virtual void onFixedUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) { }
	virtual void onLateUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) { }

	// Legacy immediate mode (deprecated, use onUpdate instead)
	virtual void run(World& world, CommandBuffer& cb) { }

	// Override to implement custom phases or read the context borrowed for a phase.
	virtual void onPhase(SystemPhase phase, World& world, CommandBuffer& cb, std::chrono::duration<double> dt, const SystemPhaseContext* context);

	// Configure phase participation before registering the system. The default
	// participation preserves the existing lifecycle, update, and legacy scheduling.
	void setPhases(std::initializer_list<SystemPhase> phases);
	[[nodiscard]] bool participatesIn(SystemPhase phase) const;

	// Configure before registration to preserve a legacy callback's immediate mutations.
	// The system runs alone and may still record explicit commands for later playback.
	void setLegacyImmediateExecution() {
		mLegacyImmediateExecution = true;
	}

	const std::string& getName() const {
		return mName;
	}
	void setName(const std::string& name) {
		mName = name;
	}

	// Set resource access and dependencies before registering the system.
	template <typename T> void readsResource() {
		mResourceReads.emplace(typeid(T));
	}
	template <typename T> void writesResource() {
		mResourceWrites.emplace(typeid(T));
	}
	template <typename T> const T& readResource(const T& resource) const {
		SystemAccessScope::requireResource(typeid(T), AccessMode::Read);
		return resource;
	}
	template <typename T> T& writeResource(T& resource) const {
		SystemAccessScope::requireResource(typeid(T), AccessMode::Write);
		return resource;
	}
	// Edges order systems within a phase, including paths through systems that
	// skip that phase. The caller controls the order in which phases run.
	void runBefore(std::string systemName) {
		mRunBefore.push_back(std::move(systemName));
	}
	void runAfter(std::string systemName) {
		mRunAfter.push_back(std::move(systemName));
	}

	// Metadata for Graph
	std::string mName;
	std::unordered_set<ComponentType> mReads;
	std::unordered_set<ComponentType> mWrites;
	// Lower values run earlier. Equal values keep registration order.
	int mPriority = 0;
	// Runs alone on the thread invoking the phase.
	bool mExclusiveBatch = false;
	// Avoid submitting a shared batch to the worker pool if every system prefers inline execution.
	bool mPreferInline = false;
	// Shared batches are always checked. Opt in to the same checks when alone.
	bool mValidateAccess = false;

	// Signature for matching (if Query based)
	Signature mQueryHas;
	Signature mQueryNot;

	// State flags
	bool mIsAwake = false;
	bool mIsStarted = false;

protected:
	void setPhaseEnabled(SystemPhase phase, bool enabled);

private:
	friend class SystemManager;
	std::bitset<static_cast<size_t>(SystemPhase::Count)> mPhases {};
	bool mLegacyImmediateExecution = false;
	std::unordered_set<std::type_index> mResourceReads;
	std::unordered_set<std::type_index> mResourceWrites;
	std::vector<std::string> mRunBefore;
	std::vector<std::string> mRunAfter;
};

class SystemManager {
public:
	struct Batch {
		std::vector<std::shared_ptr<System>> systems;
		bool preferInline = true;
	};

	explicit SystemManager(World& world, SystemBatchExecutor executor = {});

	template <typename T, typename... Args> std::shared_ptr<T> registerSystem(Args&&... args) {
		// Reject an inadmissible call before entering a user-defined constructor.
		(void)lockScheduleForChange();
		auto system = std::make_shared<T>(std::forward<Args>(args)...);
		addSystem(system);
		return system;
	}

	void addSystem(std::shared_ptr<System> system);

	[[nodiscard]] ComponentManager* getComponentManager() const noexcept;

	// A phase runs synchronously. Any context must remain valid until it returns.
	// The owning thread may register systems and enter a different phase from an unchecked callback.
	// Each phase keeps the schedule it started with. Nested phases see completed registrations.
	// Each batch applies successful callbacks' recordings in schedule order before reporting
	// its first callback failure, dispatch failure, or first playback failure (in that order).
	void runPhase(SystemPhase phase, std::chrono::duration<double> dt = {}, const SystemPhaseContext* context = nullptr);
	// Check admission before performing scene lifecycle callbacks that precede this phase.
	void requirePhaseEntry(SystemPhase phase);
	void runAwake();
	void runStart();
	void runUpdate(std::chrono::duration<double> dt);
	void runFixedUpdate(std::chrono::duration<double> dt);
	void runLateUpdate(std::chrono::duration<double> dt);
	void runDestroy();

	// Legacy immediate mode (deprecated)
	void run();

	World& getWorld() const;

	// using SystemType = Id<SystemManager>;
	//
	// SystemType mNextSystemType = IdGenerator<SystemManager>::generateId();
	//
	// template<typename T>
	// SystemType registerNewSystemId() {
	//	SystemType id = mNextSystemType;
	//	mNextSystemType = IdGenerator<SystemManager>::generateId();
	//	return id;
	// }
	//
	// template<typename T>
	// SystemType getComponentTypeImpl() {
	//	static SystemType id = registerNewSystemId<T>();
	//	std::cout << typeid(T).name() << " - " << (int)id << "\n";
	//	return id;
	// }
	//
	// template<typename T>
	// ComponentType getSystemType() {
	//	return getComponentTypeImpl<RawType<T>>();
	// }
	template <class T> std::shared_ptr<T> getSystem(const std::string& name) {
		SystemAccessScope::requireStorageAccess(*getComponentManager());
		std::lock_guard lock(mScheduleMutex);
		if (mSystems.contains(name)) {
			return std::static_pointer_cast<T>(mSystems.at(name));
		}
		return nullptr;
	}

private:
	template <typename... Components> friend class MultiPhaseSystem;

	struct PhaseSchedule {
		std::vector<Batch> batches;
		std::vector<std::shared_ptr<System>> skippedLifecycle;
	};
	using Schedule = std::array<PhaseSchedule, static_cast<size_t>(SystemPhase::Count)>;

	World& mWorld;
	SystemBatchExecutor mBatchExecutor;
	std::unordered_map<std::string, std::shared_ptr<System>> mSystems;
	std::vector<std::shared_ptr<System>> mSystemsOrder;
	std::shared_ptr<const Schedule> mSchedule;
	std::mutex mScheduleMutex;
	std::thread::id mPhaseOwner {};
	std::bitset<static_cast<size_t>(SystemPhase::Count)> mActivePhases {};
	bool mIsDirty = true;

	void rebuildBatches();
	std::unique_lock<std::mutex> lockScheduleForChange(std::optional<SystemPhase> entering = {});

	// Dependency Graph
	// std::vector<std::vector<int>> mGraph;
	// void buildGraph();
};
}
