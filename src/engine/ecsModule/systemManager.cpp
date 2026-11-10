#include "systemManager.h"
#include "commandBuffer.h"
#include "resourceModule/serviceManager.h"
#include "systemAccessConflict.h"
#include "taskModule/taskSystem.h"
#include "world.h"

#include <exception>
#include <queue>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <iostream>
#endif

namespace IKIGAI::ECS {

namespace {
	TASK::TaskSystem* getTaskSystem() {
#ifdef __EMSCRIPTEN__
		return nullptr;
#else
		if (!IKIGAI::RESOURCES::ServiceManager::Check<IKIGAI::TASK::TaskSystem>()) {
			return nullptr;
		}
		auto& tasks = RESOURCES::ServiceManager::Get<TASK::TaskSystem>();
		return !tasks.isFrameWorkerThread() && tasks.isReady() && tasks.getThreadCounts() > 0 ? &tasks : nullptr;
#endif
	}

	void WebEcsLog(const char* msg) {
#ifdef __EMSCRIPTEN__
		static int remaining = 24;
		if (remaining > 0) {
			--remaining;
			std::cout << "[web] ecs " << msg << std::endl;
			std::cout.flush();
		}
#endif
		(void)msg;
	}

} // namespace

template <typename PhaseFunc> void SystemManager::runPhase(PhaseFunc&& phaseFunc) {
	auto* tasks = getTaskSystem();
	const auto runSystem = [this, &phaseFunc, tasks](System& system, CommandBuffer& buffer, bool allowImmediateStructuralChanges) {
		if (!tasks) {
			WebEcsLog(system.getName().c_str());
		}
		ActiveCommandBufferScope scope(buffer, allowImmediateStructuralChanges && !system.mValidateAccess);
		const SystemAccessScope access(!allowImmediateStructuralChanges || system.mValidateAccess, system.getName(), *mWorld.getComponentManager(),
			system.mReads, system.mWrites, system.mResourceReads, system.mResourceWrites);
		phaseFunc(system, buffer);
	};

	for (const auto& batch : mBatches) {
		if (batch.systems.size() == 1) {
			CommandBuffer buffer;
			runSystem(*batch.systems.front(), buffer, true);
			buffer.execute(mWorld);
			continue;
		}

		std::vector<CommandBuffer> buffers(batch.systems.size());
		std::exception_ptr failure;
		const auto observe = [&failure](auto&& work) {
			try {
				work();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
		};

		if (tasks) {
			std::vector<TASK::TaskHandle<void>> handles;
			handles.reserve(batch.systems.size());
			observe([&] {
				for (size_t i = 0; i < batch.systems.size(); ++i) {
					auto* system = batch.systems[i].get();
					handles.push_back(
						tasks->submit(system->getName().c_str(), -1, nullptr, [&, system, i] { runSystem(*system, buffers[i], false); }));
				}
			});
			// Join all submitted systems before their captures go out of scope.
			for (const auto& handle : handles) {
				observe([&] { handle.mFuture->get(); });
			}
		}
		else {
			for (size_t i = 0; i < batch.systems.size(); ++i) {
				observe([&] { runSystem(*batch.systems[i], buffers[i], false); });
			}
		}

		if (failure) {
			std::rethrow_exception(failure);
		}
		// Structural edits become visible at the same boundary in either mode.
		for (auto& buffer : buffers) {
			buffer.execute(mWorld);
		}
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

} // namespace

bool systemsAccessConflict(const SystemScheduleView& left, const SystemScheduleView& right) noexcept {
	return !left.declaresAccess() || !right.declaresAccess() || accessSetsConflict(left.reads, left.writes, right.reads, right.writes)
		|| accessSetsConflict(left.resourceReads, left.resourceWrites, right.resourceReads, right.resourceWrites);
}

// Batch entries are indexes into `systems` (registration order).
std::vector<std::vector<size_t>> buildSystemBatches(const std::vector<SystemScheduleView>& systems) {
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

	std::vector<std::vector<size_t>> batches;
	size_t scheduled = 0;
	while (!ready.empty()) {
		const size_t index = ready.top();
		ready.pop();
		++scheduled;
		const SystemScheduleView& system = systems[index];
		const auto blocksSameBatch = [&](size_t existing) {
			return systems[existing].exclusiveBatch || systemsAccessConflict(system, systems[existing])
				|| std::ranges::find(system.predecessors, existing) != system.predecessors.end();
		};
		if (system.exclusiveBatch || batches.empty() || std::ranges::any_of(batches.back(), blocksSameBatch)) {
			batches.push_back({});
		}
		batches.back().push_back(index);

		for (const size_t successor : successors[index]) {
			if (--pending[successor] == 0) {
				ready.push(successor);
			}
		}
	}
	if (scheduled != systems.size()) {
		std::string message = "ECS system dependency cycle blocks";
		for (size_t index = 0; index < systems.size(); ++index) {
			if (pending[index] != 0) {
				message += " ";
				message += systems[index].name;
			}
		}
		throw std::logic_error(message);
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

	std::vector<SystemScheduleView> views;
	views.reserve(mSystemsOrder.size());
	for (size_t index = 0; index < mSystemsOrder.size(); ++index) {
		const auto& system = *mSystemsOrder[index];
		views.push_back(SystemScheduleView { system.getName(), system.mPriority, system.mExclusiveBatch, system.mReads, system.mWrites,
			system.mResourceReads, system.mResourceWrites, predecessors[index] });
	}

	std::vector<Batch> batches;
	for (const std::vector<size_t>& indexes : buildSystemBatches(views)) {
		Batch batch;
		batch.systems.reserve(indexes.size());
		for (const size_t index : indexes) {
			batch.systems.push_back(mSystemsOrder[index]);
		}
		batches.push_back(std::move(batch));
	}
	mBatches = std::move(batches);
	mIsDirty = false;
}

ComponentManager* SystemManager::getComponentManager() const noexcept {
	return mWorld.getComponentManager();
}

void SystemManager::runAwake() {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhase([this](System& system, CommandBuffer&) {
		if (!system.mIsAwake) {
			system.onAwake(mWorld);
			system.mIsAwake = true;
		}
	});
}

void SystemManager::runStart() {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhase([this](System& system, CommandBuffer&) {
		if (system.mIsAwake && !system.mIsStarted) {
			system.onStart(mWorld);
			system.mIsStarted = true;
		}
	});
}

void SystemManager::runUpdate(std::chrono::duration<double> dt) {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhase([this, dt](System& system, CommandBuffer& buffer) { system.onUpdate(mWorld, buffer, dt); });
}

void SystemManager::runFixedUpdate(std::chrono::duration<double> dt) {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhase([this, dt](System& system, CommandBuffer& buffer) { system.onFixedUpdate(mWorld, buffer, dt); });
}

void SystemManager::runLateUpdate(std::chrono::duration<double> dt) {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhase([this, dt](System& system, CommandBuffer& buffer) { system.onLateUpdate(mWorld, buffer, dt); });
}

void SystemManager::runDestroy() {
	runPhase([this](System& system, CommandBuffer&) {
		if (system.mIsStarted) {
			system.onDestroy(mWorld);
			system.mIsStarted = false;
			system.mIsAwake = false;
		}
	});
}

void SystemManager::run() {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhase([this](System& system, CommandBuffer& buffer) { system.run(mWorld, buffer); });
}

World& SystemManager::getWorld() const {
	return mWorld;
}
}
