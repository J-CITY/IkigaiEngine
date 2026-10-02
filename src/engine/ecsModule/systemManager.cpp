#include "systemManager.h"
#include "world.h"
#include "commandBuffer.h"
#include <resourceModule/serviceManager.h>
#include <taskModule/taskSystem.h>
#include <future>
#ifdef __EMSCRIPTEN__
#include <iostream>
#endif

namespace IKIGAI::ECS2 {

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
		mBatches.clear();
		for (const auto& [name, sys] : mSystems) {
			bool added = false;
			// Try to add to the last batch if exists
			if (!mBatches.empty()) {
				auto& batch = mBatches.back();
				bool conflict = false;
				for (const auto& existingSys : batch.systems) {
					// Check conflicts
					// Conflict if sys Writes to something existingSys Reads or Writes
					for (auto id : sys->mWrites) {
						if (existingSys->mReads.contains(id) || existingSys->mWrites.contains(id)) {
							conflict = true; break;
						}
					}
					if (conflict) break;
					
					// Conflict if sys Reads something existingSys Writes
					for (auto id : sys->mReads) {
						if (existingSys->mWrites.contains(id)) {
							conflict = true; break;
						}
					}
					if (conflict) break;
				}

				if (!conflict) {
					batch.systems.push_back(sys);
					added = true;
				}
			}

			if (!added) {
				Batch newBatch;
				newBatch.systems.push_back(sys);
				mBatches.push_back(std::move(newBatch));
			}
		}
		mIsDirty = false;
	}

	// Template helper for running phases with batching and multithreading
	template<typename PhaseFunc>
	void runPhaseImpl(std::vector<SystemManager::Batch>& batches, World& world, PhaseFunc&& phaseFunc) {
		if (UseTaskSystem()) {
			auto& ts = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
			
			for (auto& batch : batches) {
				if (batch.systems.size() == 1) {
					phaseFunc(batch.systems[0].get(), world);
				}
				else {
					std::vector<IKIGAI::TASK::TaskHandle<void>> handles;
					handles.reserve(batch.systems.size());
					
					for (auto& sys : batch.systems) {
						handles.push_back(ts.submit(sys->mName.c_str(), -1, nullptr, 
							[s = sys.get(), &w = world, &phaseFunc]() {
								phaseFunc(s, w);
							}
						));
					}
					
					for (auto& h : handles) {
						if(h.mFuture) h.mFuture->wait();
					}
				}
			}
		}
		else {
			// Fallback single threaded
			for (auto& batch : batches) {
				for (auto& sys : batch.systems) {
					phaseFunc(sys.get(), world);
				}
			}
		}
	}

	ComponentManager* SystemManager::getComponentManager() const noexcept
	{
		return mWorld.getComponentManager();
	}

	void SystemManager::runAwake() {
		if (mIsDirty) rebuildBatches();
		
		runPhaseImpl(mBatches, mWorld, [](System* sys, World& w) {
			if (!sys->mIsAwake) {
				sys->onAwake(w);
				sys->mIsAwake = true;
			}
		});
	}

	void SystemManager::runStart() {
		if (mIsDirty) rebuildBatches();
		
		runPhaseImpl(mBatches, mWorld, [](System* sys, World& w) {
			if (sys->mIsAwake && !sys->mIsStarted) {
				sys->onStart(w);
				sys->mIsStarted = true;
			}
		});
	}

	// Template helper for update phases (with dt and CommandBuffer)
	template<typename PhaseFunc>
	void runUpdatePhaseImpl(std::vector<SystemManager::Batch>& batches, World& world, std::chrono::duration<double> dt, PhaseFunc&& phaseFunc) {
		if (UseTaskSystem()) {
			auto& ts = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
			
			for (auto& batch : batches) {
				if (batch.systems.size() == 1) {
					CommandBuffer cb;
					phaseFunc(batch.systems[0].get(), world, cb, dt);
					cb.execute();
				}
				else {
					std::vector<CommandBuffer> buffers(batch.systems.size());
					std::vector<IKIGAI::TASK::TaskHandle<void>> handles;
					handles.reserve(batch.systems.size());
					
					int i = 0;
					for (auto& sys : batch.systems) {
						handles.push_back(ts.submit(sys->mName.c_str(), -1, nullptr, 
							[s = sys.get(), &w = world, &cb = buffers[i], dt, &phaseFunc]() {
								phaseFunc(s, w, cb, dt);
							}
						));
						i++;
					}
					
					for (auto& h : handles) {
						if(h.mFuture) h.mFuture->wait();
					}
					
					for (auto& cb : buffers) {
						cb.execute();
					}
				}
			}
		}
		else {
			CommandBuffer cb;
			for (auto& batch : batches) {
				for (auto& sys : batch.systems) {
					WebEcsLog(sys->mName.c_str());
					phaseFunc(sys.get(), world, cb, dt);
				}
			}
			cb.execute();
		}
	}

	void SystemManager::runUpdate(std::chrono::duration<double> dt) {
		if (mIsDirty) rebuildBatches();
		
		runUpdatePhaseImpl(mBatches, mWorld, dt, [](System* s, World& w, CommandBuffer& cb, std::chrono::duration<double> dt) {
			s->onUpdate(w, cb, dt);
		});
	}

	void SystemManager::runFixedUpdate(std::chrono::duration<double> dt) {
		if (mIsDirty) rebuildBatches();
		
		runUpdatePhaseImpl(mBatches, mWorld, dt, [](System* s, World& w, CommandBuffer& cb, std::chrono::duration<double> dt) {
			s->onFixedUpdate(w, cb, dt);
		});
	}

	void SystemManager::runLateUpdate(std::chrono::duration<double> dt) {
		if (mIsDirty) rebuildBatches();
		
		runUpdatePhaseImpl(mBatches, mWorld, dt, [](System* s, World& w, CommandBuffer& cb, std::chrono::duration<double> dt) {
			s->onLateUpdate(w, cb, dt);
		});
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

	// Legacy run() method - calls run() on each system
	void SystemManager::run() {
		if (mIsDirty) {
			rebuildBatches();
		}

		if (UseTaskSystem()) {
			auto& ts = IKIGAI::RESOURCES::ServiceManager::Get<IKIGAI::TASK::TaskSystem>();
			
			for (auto& batch : mBatches) {
				if (batch.systems.size() == 1) {
					CommandBuffer cb;
					batch.systems[0]->run(mWorld, cb);
					cb.execute();
				}
				else {
					std::vector<CommandBuffer> buffers(batch.systems.size());
					std::vector<IKIGAI::TASK::TaskHandle<void>> handles;
					handles.reserve(batch.systems.size());
					
					int i = 0;
					for (auto& sys : batch.systems) {
						handles.push_back(ts.submit(sys->mName.c_str(), -1, nullptr, 
							[s = sys.get(), &w = mWorld, &cb = buffers[i]]() {
								s->run(w, cb);
							}
						));
						i++;
					}
					
					for (auto& h : handles) {
						if(h.mFuture) h.mFuture->wait();
					}
					
					for (auto& cb : buffers) {
						cb.execute();
					}
				}
			}
		}
		else {
			CommandBuffer cb;
			for (auto& batch : mBatches) {
				for (auto& sys : batch.systems) {
					sys->run(mWorld, cb);
				}
			}
			cb.execute();
		}
	}

	World& SystemManager::getWorld() const {
		return mWorld;
	}
}
