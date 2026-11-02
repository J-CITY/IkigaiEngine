#include "systemManager.h"
#include "commandBuffer.h"
#include "resourceModule/serviceManager.h"
#include "systemAccessConflict.h"
#include "taskModule/taskSystem.h"
#include "world.h"

#ifdef __EMSCRIPTEN__
#include <iostream>
#endif

namespace IKIGAI::ECS {

bool UseTaskSystem() {
#ifdef __EMSCRIPTEN__
	return false;
#else
	if (!IKIGAI::RESOURCES::ServiceManager::Check<IKIGAI::TASK::TaskSystem>()) {
		return false;
	}
	return IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>().isReady();
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

void SystemManager::rebuildBatches() {
	std::vector<SystemScheduleView> views;
	views.reserve(mSystemsOrder.size());
	for (const auto& sys : mSystemsOrder) {
		views.push_back(SystemScheduleView { sys->mPriority, sys->mExclusiveBatch, &sys->mReads, &sys->mWrites });
	}

	mBatches.clear();
	for (const std::vector<size_t>& indexes : buildSystemBatches(views)) {
		Batch batch;
		batch.systems.reserve(indexes.size());
		for (const size_t index : indexes) {
			batch.systems.push_back(mSystemsOrder[index]);
		}
		mBatches.push_back(std::move(batch));
	}
	mIsDirty = false;
}

template <typename PhaseFunc> void runPhaseImpl(std::vector<SystemManager::Batch>& batches, World& world, PhaseFunc&& phaseFunc) {
	if (UseTaskSystem()) {
		auto& tasks = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
		for (auto& batch : batches) {
			if (batch.systems.size() == 1) {
				phaseFunc(batch.systems[0].get(), world);
				continue;
			}
			std::vector<IKIGAI::TASK::TaskHandle<void>> handles;
			handles.reserve(batch.systems.size());
			for (auto& sys : batch.systems) {
				handles.push_back(tasks.submit(sys->mName.c_str(), -1, nullptr, [s = sys.get(), &world, &phaseFunc]() { phaseFunc(s, world); }));
			}
			for (auto& handle : handles) {
				if (handle.mFuture) {
					handle.mFuture->wait();
				}
			}
		}
		return;
	}

	for (auto& batch : batches) {
		for (auto& sys : batch.systems) {
			phaseFunc(sys.get(), world);
		}
	}
}

ComponentManager* SystemManager::getComponentManager() const noexcept {
	return mWorld.getComponentManager();
}

void SystemManager::runAwake() {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhaseImpl(mBatches, mWorld, [](System* sys, World& w) {
		if (!sys->mIsAwake) {
			sys->onAwake(w);
			sys->mIsAwake = true;
		}
	});
}

void SystemManager::runStart() {
	if (mIsDirty) {
		rebuildBatches();
	}

	runPhaseImpl(mBatches, mWorld, [](System* sys, World& w) {
		if (sys->mIsAwake && !sys->mIsStarted) {
			sys->onStart(w);
			sys->mIsStarted = true;
		}
	});
}

template <typename PhaseFunc>
void runUpdatePhaseImpl(std::vector<SystemManager::Batch>& batches, World& world, std::chrono::duration<double> dt, PhaseFunc&& phaseFunc) {
	if (UseTaskSystem()) {
		auto& tasks = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
		for (auto& batch : batches) {
			if (batch.systems.size() == 1) {
				CommandBuffer cb;
				phaseFunc(batch.systems[0].get(), world, cb, dt);
				cb.execute();
				continue;
			}
			std::vector<CommandBuffer> buffers(batch.systems.size());
			std::vector<IKIGAI::TASK::TaskHandle<void>> handles;
			handles.reserve(batch.systems.size());
			for (size_t i = 0; i < batch.systems.size(); ++i) {
				auto* sys = batch.systems[i].get();
				handles.push_back(tasks.submit(
					sys->mName.c_str(), -1, nullptr, [sys, &world, &cb = buffers[i], dt, &phaseFunc]() { phaseFunc(sys, world, cb, dt); }));
			}
			for (auto& handle : handles) {
				if (handle.mFuture) {
					handle.mFuture->wait();
				}
			}
			for (auto& cb : buffers) {
				cb.execute();
			}
		}
		return;
	}

	CommandBuffer cb;
	for (auto& batch : batches) {
		for (auto& sys : batch.systems) {
			WebEcsLog(sys->mName.c_str());
			phaseFunc(sys.get(), world, cb, dt);
		}
	}
	cb.execute();
}

void SystemManager::runUpdate(std::chrono::duration<double> dt) {
	if (mIsDirty) {
		rebuildBatches();
	}

	runUpdatePhaseImpl(
		mBatches, mWorld, dt, [](System* s, World& w, CommandBuffer& cb, std::chrono::duration<double> dt) { s->onUpdate(w, cb, dt); });
}

void SystemManager::runFixedUpdate(std::chrono::duration<double> dt) {
	if (mIsDirty) {
		rebuildBatches();
	}

	runUpdatePhaseImpl(
		mBatches, mWorld, dt, [](System* s, World& w, CommandBuffer& cb, std::chrono::duration<double> dt) { s->onFixedUpdate(w, cb, dt); });
}

void SystemManager::runLateUpdate(std::chrono::duration<double> dt) {
	if (mIsDirty) {
		rebuildBatches();
	}

	runUpdatePhaseImpl(
		mBatches, mWorld, dt, [](System* s, World& w, CommandBuffer& cb, std::chrono::duration<double> dt) { s->onLateUpdate(w, cb, dt); });
}

void SystemManager::runDestroy() {
	runPhaseImpl(mBatches, mWorld, [](System* sys, World& w) {
		if (sys->mIsStarted) {
			sys->onDestroy(w);
			sys->mIsStarted = false;
			sys->mIsAwake = false;
		}
	});
}

void SystemManager::run() {
	if (mIsDirty) {
		rebuildBatches();
	}

	if (UseTaskSystem()) {
		auto& tasks = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
		for (auto& batch : mBatches) {
			if (batch.systems.size() == 1) {
				CommandBuffer cb;
				batch.systems[0]->run(mWorld, cb);
				cb.execute();
				continue;
			}
			std::vector<CommandBuffer> buffers(batch.systems.size());
			std::vector<IKIGAI::TASK::TaskHandle<void>> handles;
			handles.reserve(batch.systems.size());
			for (size_t i = 0; i < batch.systems.size(); ++i) {
				auto* sys = batch.systems[i].get();
				handles.push_back(tasks.submit(sys->mName.c_str(), -1, nullptr, [sys, &world = mWorld, &cb = buffers[i]]() { sys->run(world, cb); }));
			}
			for (auto& handle : handles) {
				if (handle.mFuture) {
					handle.mFuture->wait();
				}
			}
			for (auto& cb : buffers) {
				cb.execute();
			}
		}
		return;
	}

	CommandBuffer cb;
	for (auto& batch : mBatches) {
		for (auto& sys : batch.systems) {
			sys->run(mWorld, cb);
		}
	}
	cb.execute();
}

World& SystemManager::getWorld() const {
	return mWorld;
}
}
