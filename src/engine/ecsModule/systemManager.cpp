#include "systemManager.h"
#include "commandBuffer.h"
#include "systemAccessConflict.h"
#include "world.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <queue>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <atomic>
#include <iostream>
#endif

namespace IKIGAI::ECS {

namespace {
	struct SystemJobResult {
		CommandBuffer buffer {};
		std::exception_ptr failure {};
		bool succeeded = false;
	};

	constexpr size_t phaseIndex(SystemPhase phase) {
		return static_cast<size_t>(phase);
	}

	void WebEcsLog(const char* msg) {
#ifdef __EMSCRIPTEN__
		static std::atomic<int> remaining { 24 };
		if (remaining.load(std::memory_order_relaxed) > 0 && remaining.fetch_sub(1, std::memory_order_relaxed) > 0) {
			std::cout << "[web] ecs " << msg << std::endl;
			std::cout.flush();
		}
#endif
		(void)msg;
	}

} // namespace

System::System() {
	setPhases({ SystemPhase::Awake, SystemPhase::Start, SystemPhase::FixedUpdate, SystemPhase::Update, SystemPhase::LateUpdate, SystemPhase::Destroy,
		SystemPhase::Legacy });
}

void System::setPhases(std::initializer_list<SystemPhase> phases) {
	decltype(mPhases) enabled {};
	for (const auto phase : phases) {
		enabled.set(phaseIndex(phase));
	}
	mPhases = enabled;
}

bool System::participatesIn(SystemPhase phase) const {
	return mPhases.test(phaseIndex(phase));
}

void System::setPhaseEnabled(SystemPhase phase, bool enabled) {
	mPhases.set(phaseIndex(phase), enabled);
}

void System::onPhase(SystemPhase phase, World& world, CommandBuffer& buffer, std::chrono::duration<double> dt, const SystemPhaseContext*) {
	switch (phase) {
	case SystemPhase::Awake:
		onAwake(world);
		break;
	case SystemPhase::Start:
		onStart(world);
		break;
	case SystemPhase::FixedUpdate:
		onFixedUpdate(world, buffer, dt);
		break;
	case SystemPhase::Update:
		onUpdate(world, buffer, dt);
		break;
	case SystemPhase::LateUpdate:
		onLateUpdate(world, buffer, dt);
		break;
	case SystemPhase::Destroy:
		onDestroy(world);
		break;
	case SystemPhase::Legacy:
		run(world, buffer);
		break;
	default:
		break;
	}
}

SystemManager::SystemManager(World& world, SystemBatchExecutor executor)
	: mWorld(world)
	, mBatchExecutor(std::move(executor)) { }

std::unique_lock<std::mutex> SystemManager::lockScheduleForChange(std::optional<SystemPhase> entering) {
	SystemAccessScope::requireStorageAccess(*mWorld.getComponentManager());
	if (!immediateStructuralChangesAllowed() || (SystemAccessScope::sCurrent && SystemAccessScope::sCurrent->mChecked)) {
		throw std::logic_error("A checked ECS callback cannot register systems or enter another phase");
	}
	std::unique_lock lock(mScheduleMutex);
	if (mActivePhases.any() && mPhaseOwner != std::this_thread::get_id()) {
		throw std::logic_error("An active ECS schedule may only be changed or entered by its owning thread");
	}
	if (entering && mActivePhases.test(phaseIndex(*entering))) {
		throw std::logic_error("Cannot recursively enter the same ECS phase");
	}
	return lock;
}

void SystemManager::requirePhaseEntry(SystemPhase phase) {
	(void)lockScheduleForChange(phase);
}

void SystemManager::addSystem(std::shared_ptr<System> system) {
	std::shared_ptr<System> replaced;
	const auto lock = lockScheduleForChange();
	if (!system) {
		throw std::invalid_argument("Cannot register a null ECS system");
	}
	const auto& name = system->getName();
	auto [entry, inserted] = mSystems.try_emplace(name, system);
	if (inserted) {
		try {
			mSystemsOrder.push_back(system);
		} catch (...) {
			mSystems.erase(entry);
			throw;
		}
	}
	else {
		// Release the old instance after unlocking: a system destructor may call back into the manager.
		replaced = std::exchange(entry->second, system);
		const auto existing = std::ranges::find(mSystemsOrder, replaced);
		*existing = std::move(system);
	}
	mIsDirty = true;
}

void SystemManager::runPhase(SystemPhase phase, std::chrono::duration<double> dt, const SystemPhaseContext* context) {
	const auto index = phaseIndex(phase);
	std::shared_ptr<const Schedule> schedule;
	std::shared_ptr<const Schedule> previous;
	{
		const auto lock = lockScheduleForChange(phase);
		if (mIsDirty) {
			previous = mSchedule;
			rebuildBatches();
		}
		schedule = mSchedule;
		mActivePhases.set(index);
		mPhaseOwner = std::this_thread::get_id();
	}
	struct LeavePhase {
		SystemManager& manager;
		size_t index;
		~LeavePhase() {
			std::lock_guard lock(manager.mScheduleMutex);
			manager.mActivePhases.reset(index);
		}
	} leavePhase { *this, index };
	previous.reset();

	const auto phaseCallback = [phase](System& system, auto&& invoke) {
		switch (phase) {
		case SystemPhase::Awake:
			if (!system.mIsAwake) {
				invoke();
				system.mIsAwake = true;
			}
			break;
		case SystemPhase::Start:
			if (system.mIsAwake && !system.mIsStarted) {
				invoke();
				system.mIsStarted = true;
			}
			break;
		case SystemPhase::Destroy:
			if (system.mIsStarted) {
				invoke();
				system.mIsStarted = false;
				system.mIsAwake = false;
			}
			break;
		default:
			invoke();
			break;
		}
	};
	const auto runSystem = [&](System& system, CommandBuffer& buffer, bool allowImmediateStructuralChanges) {
		WebEcsLog(system.getName().c_str());
		std::optional<ActiveCommandBufferScope> commandScope;
		if (!system.mLegacyImmediateExecution || !allowImmediateStructuralChanges) {
			commandScope.emplace(buffer, allowImmediateStructuralChanges && !system.mValidateAccess);
		}
		const SystemAccessScope access(!allowImmediateStructuralChanges || system.mValidateAccess, system.getName(), *mWorld.getComponentManager(),
			system.mReads, system.mWrites, system.mResourceReads, system.mResourceWrites);
		std::optional<ComponentManager::StorageScope> storage;
		if (!allowImmediateStructuralChanges || system.mValidateAccess) {
			storage.emplace(*mWorld.getComponentManager(), ComponentManager::StorageAccess::Query);
		}
		phaseCallback(system, [&] { system.onPhase(phase, mWorld, buffer, dt, context); });
	};

	const auto& phaseSchedule = schedule->at(index);
	for (const auto& batch : phaseSchedule.batches) {
		if (batch.systems.size() == 1) {
			CommandBuffer buffer;
			runSystem(*batch.systems.front(), buffer, true);
			buffer.execute(mWorld);
			continue;
		}

		std::vector<SystemJobResult> results(batch.systems.size());
		// Each job owns a slot. The executor joins all admitted jobs before these results are read.
		const auto runJob = [&](std::size_t index) noexcept {
			auto& result = results[index];
			try {
				runSystem(*batch.systems[index], result.buffer, false);
				result.succeeded = true;
			} catch (...) {
				result.failure = std::current_exception();
			}
		};
		std::exception_ptr dispatchFailure {};
		bool runInline = true;
		if (mBatchExecutor && !batch.preferInline && !mWorld.getComponentManager()->hasStructuralChangeOnThisThread()) {
			std::vector<const char*> names;
			names.reserve(batch.systems.size());
			for (const auto& system : batch.systems) {
				names.push_back(system->getName().c_str());
			}
			try {
				const SystemAccessScope::BatchDispatch dispatch(*mWorld.getComponentManager());
				runInline = !mBatchExecutor(names, runJob);
				if (runInline && std::ranges::any_of(results, [](const auto& result) { return result.succeeded || result.failure; })) {
					throw std::logic_error("ECS batch executor requested inline execution after starting jobs");
				}
			} catch (...) {
				dispatchFailure = std::current_exception();
				runInline = false;
			}
		}
		if (runInline) {
			for (std::size_t index = 0; index < batch.systems.size(); ++index) {
				runJob(index);
			}
		}

		// Commit successful recordings in schedule order, even if a sibling system or buffer failed.
		std::exception_ptr systemFailure {};
		std::exception_ptr playbackFailure {};
		bool missingJob = false;
		for (auto& result : results) {
			if (result.failure) {
				if (!systemFailure) {
					systemFailure = result.failure;
				}
				continue;
			}
			if (!result.succeeded) {
				missingJob = true;
				continue;
			}
			try {
				result.buffer.execute(mWorld);
			} catch (...) {
				if (!playbackFailure) {
					playbackFailure = std::current_exception();
				}
			}
		}
		// Report callback failures first, then dispatch failures, then command playback failures.
		if (systemFailure) {
			std::rethrow_exception(systemFailure);
		}
		if (dispatchFailure) {
			std::rethrow_exception(dispatchFailure);
		}
		if (missingJob) {
			throw std::logic_error("ECS batch executor completed without running all jobs");
		}
		if (playbackFailure) {
			std::rethrow_exception(playbackFailure);
		}
	}

	// A system can skip an Awake or Start callback and still reach later lifecycle stages.
	for (const auto& system : phaseSchedule.skippedLifecycle) {
		phaseCallback(*system, [] { });
	}
}

namespace {
	template <typename Key>
	[[nodiscard]] bool accessSetsConflict(const std::unordered_set<Key>& readsA, const std::unordered_set<Key>& writesA,
		const std::unordered_set<Key>& readsB, const std::unordered_set<Key>& writesB) noexcept {
		for (const auto& id : writesA) {
			if (readsB.contains(id) || writesB.contains(id)) {
				return true;
			}
		}

		for (const auto& id : readsA) {
			if (writesB.contains(id)) {
				return true;
			}
		}

		return false;
	}

	[[nodiscard]] std::vector<size_t> orderSystems(const std::vector<SystemScheduleView>& systems) {
		std::vector<std::vector<size_t>> successors(systems.size());
		std::vector<size_t> pending(systems.size());
		// Among ready systems, lower priority values and earlier registrations run first.
		const auto later = [&systems](size_t left, size_t right) {
			return systems[left].priority == systems[right].priority ? left > right : systems[left].priority > systems[right].priority;
		};
		std::priority_queue<size_t, std::vector<size_t>, decltype(later)> ready(later);
		for (size_t index = 0; index < systems.size(); ++index) {
			pending[index] = systems[index].predecessors.size();
			for (const size_t predecessor : systems[index].predecessors) {
				successors[predecessor].push_back(index);
			}
			if (pending[index] == 0) {
				ready.push(index);
			}
		}

		std::vector<size_t> ordered;
		ordered.reserve(systems.size());
		while (!ready.empty()) {
			const size_t index = ready.top();
			ready.pop();
			ordered.push_back(index);
			for (const size_t successor : successors[index]) {
				if (--pending[successor] == 0) {
					ready.push(successor);
				}
			}
		}
		if (ordered.size() != systems.size()) {
			std::string message = "ECS system dependency cycle blocks";
			for (size_t index = 0; index < systems.size(); ++index) {
				if (pending[index] != 0) {
					message += " ";
					message += systems[index].name;
				}
			}
			throw std::logic_error(message);
		}
		return ordered;
	}

} // namespace

bool systemsAccessConflict(const SystemScheduleView& left, const SystemScheduleView& right) noexcept {
	return !left.declaresAccess() || !right.declaresAccess() || accessSetsConflict(left.reads, left.writes, right.reads, right.writes)
		|| accessSetsConflict(left.resourceReads, left.resourceWrites, right.resourceReads, right.resourceWrites);
}

// Batch entries are indexes into `systems` (registration order).
std::vector<std::vector<size_t>> buildSystemBatches(const std::vector<SystemScheduleView>& systems) {
	std::vector<std::vector<size_t>> batches;
	for (const size_t index : orderSystems(systems)) {
		const SystemScheduleView& system = systems[index];
		const auto blocksSameBatch = [&](size_t existing) {
			return systems[existing].exclusiveBatch || systemsAccessConflict(system, systems[existing])
				|| std::ranges::find(system.predecessors, existing) != system.predecessors.end();
		};
		if (system.exclusiveBatch || batches.empty() || std::ranges::any_of(batches.back(), blocksSameBatch)) {
			batches.push_back({});
		}
		batches.back().push_back(index);
	}
	return batches;
}

void SystemManager::rebuildBatches() {
	std::unordered_map<std::string_view, size_t> indexesByName;
	for (size_t index = 0; index < mSystemsOrder.size(); ++index) {
		indexesByName.emplace(mSystemsOrder[index]->getName(), index);
	}

	std::vector<std::vector<size_t>> predecessors(mSystemsOrder.size());
	for (size_t index = 0; index < mSystemsOrder.size(); ++index) {
		const auto& system = *mSystemsOrder[index];
		const auto findDependency = [&](const std::string& name) {
			const auto dependency = indexesByName.find(name);
			if (dependency == indexesByName.end()) {
				throw std::logic_error("ECS system '" + system.getName() + "' depends on unknown system '" + name + "'");
			}
			return dependency->second;
		};
		for (const auto& name : system.mRunAfter) {
			predecessors[index].push_back(findDependency(name));
		}
		for (const auto& name : system.mRunBefore) {
			predecessors[findDependency(name)].push_back(index);
		}
	}

	const auto makeView = [](const System& system, std::span<const size_t> dependencies) {
		return SystemScheduleView { system.getName(), system.mPriority, system.mExclusiveBatch || system.mLegacyImmediateExecution, system.mReads,
			system.mWrites, system.mResourceReads, system.mResourceWrites, dependencies };
	};
	std::vector<SystemScheduleView> allSystems;
	allSystems.reserve(mSystemsOrder.size());
	for (size_t index = 0; index < mSystemsOrder.size(); ++index) {
		allSystems.push_back(makeView(*mSystemsOrder[index], predecessors[index]));
	}
	// Reject contradictory runBefore/runAfter metadata even across inactive phases.
	(void)orderSystems(allSystems);

	auto schedule = std::make_shared<Schedule>();
	const size_t inactive = mSystemsOrder.size();
	for (size_t phase = 0; phase < schedule->size(); ++phase) {
		auto& phaseSchedule = schedule->at(phase);
		const auto systemPhase = static_cast<SystemPhase>(phase);
		std::vector<size_t> activeSystems;
		std::vector<size_t> localIndex(mSystemsOrder.size(), inactive);
		for (size_t index = 0; index < mSystemsOrder.size(); ++index) {
			if (mSystemsOrder[index]->participatesIn(systemPhase)) {
				localIndex[index] = activeSystems.size();
				activeSystems.push_back(index);
			}
			else if (systemPhase == SystemPhase::Awake || systemPhase == SystemPhase::Start || systemPhase == SystemPhase::Destroy) {
				phaseSchedule.skippedLifecycle.push_back(mSystemsOrder[index]);
			}
		}
		if (activeSystems.empty()) {
			continue;
		}

		std::vector<std::vector<size_t>> phasePredecessors(activeSystems.size());
		std::vector<SystemScheduleView> views;
		views.reserve(activeSystems.size());
		std::vector<size_t> visited(mSystemsOrder.size(), inactive);
		std::vector<size_t> pending;
		for (size_t index = 0; index < activeSystems.size(); ++index) {
			const size_t activeIndex = activeSystems[index];
			auto& dependencies = phasePredecessors[index];
			pending.assign(predecessors[activeIndex].begin(), predecessors[activeIndex].end());
			while (!pending.empty()) {
				const size_t predecessor = pending.back();
				pending.pop_back();
				if (visited[predecessor] == index) {
					continue;
				}
				visited[predecessor] = index;
				if (localIndex[predecessor] != inactive) {
					dependencies.push_back(localIndex[predecessor]);
				}
				else {
					// Preserve ordering through a node that has no callback in this phase.
					pending.insert(pending.end(), predecessors[predecessor].begin(), predecessors[predecessor].end());
				}
			}
			views.push_back(makeView(*mSystemsOrder[activeIndex], dependencies));
		}

		for (const auto& indexes : buildSystemBatches(views)) {
			Batch batch;
			batch.systems.reserve(indexes.size());
			for (const size_t index : indexes) {
				const auto& system = mSystemsOrder[activeSystems[index]];
				batch.systems.push_back(system);
				batch.preferInline = batch.preferInline && system->mPreferInline;
			}
			phaseSchedule.batches.push_back(std::move(batch));
		}
	}
	mSchedule = std::move(schedule);
	mIsDirty = false;
}

ComponentManager* SystemManager::getComponentManager() const noexcept {
	return mWorld.getComponentManager();
}

void SystemManager::runAwake() {
	runPhase(SystemPhase::Awake);
}

void SystemManager::runStart() {
	runPhase(SystemPhase::Start);
}

void SystemManager::runUpdate(std::chrono::duration<double> dt) {
	runPhase(SystemPhase::Update, dt);
}

void SystemManager::runFixedUpdate(std::chrono::duration<double> dt) {
	runPhase(SystemPhase::FixedUpdate, dt);
}

void SystemManager::runLateUpdate(std::chrono::duration<double> dt) {
	runPhase(SystemPhase::LateUpdate, dt);
}

void SystemManager::runDestroy() {
	runPhase(SystemPhase::Destroy);
}

void SystemManager::run() {
	runPhase(SystemPhase::Legacy);
}

World& SystemManager::getWorld() const {
	return mWorld;
}
}
