#pragma once
#include "systemManager.h"
#include "world.h"

namespace IKIGAI::ECS {

template <typename... Components> class MultiPhaseSystem : public System {
public:
	// Function types for different phases
	using InitFunc = std::function<void(Entity, QueryReference<Components>...)>;
	using UpdateFunc = std::function<void(Entity, QueryReference<Components>..., std::chrono::duration<double>)>;

private:
	InitFunc mOnAwake;
	InitFunc mOnStart;
	UpdateFunc mOnUpdate;
	UpdateFunc mOnFixedUpdate;
	UpdateFunc mOnLateUpdate;
	InitFunc mOnDestroy;

public:
	MultiPhaseSystem(const std::string& name, SystemManager& mgr) {
		mName = name;
		auto& cm = *mgr.getWorld().getComponentManager();
		// Populate dependency tracking
		((AccessTraits<Components>::ReadOnly ? mReads.insert(cm.getComponentType<Components>()) : mWrites.insert(cm.getComponentType<Components>())),
			...);
	}

	// Setters for each phase
	void setOnAwake(InitFunc&& f) {
		mOnAwake = std::move(f);
	}
	void setOnStart(InitFunc&& f) {
		mOnStart = std::move(f);
	}
	void setOnUpdate(UpdateFunc&& f) {
		mOnUpdate = std::move(f);
	}
	void setOnFixedUpdate(UpdateFunc&& f) {
		mOnFixedUpdate = std::move(f);
	}
	void setOnLateUpdate(UpdateFunc&& f) {
		mOnLateUpdate = std::move(f);
	}
	void setOnDestroy(InitFunc&& f) {
		mOnDestroy = std::move(f);
	}

	// System interface implementations
	void onAwake(World& world) override {
		if (mOnAwake) {
			world.getComponentManager()->forEach<Components...>(mOnAwake);
		}
	}

	void onStart(World& world) override {
		if (mOnStart) {
			world.getComponentManager()->forEach<Components...>(mOnStart);
		}
	}

	void onUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override {
		if (mOnUpdate) {
			// Wrap the user function to inject dt
			world.getComponentManager()->forEach<Components...>([&](Entity e, QueryReference<Components>... comps) { mOnUpdate(e, comps..., dt); });
		}
	}

	void onFixedUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override {
		if (mOnFixedUpdate) {
			world.getComponentManager()->forEach<Components...>(
				[&](Entity e, QueryReference<Components>... comps) { mOnFixedUpdate(e, comps..., dt); });
		}
	}

	void onLateUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override {
		if (mOnLateUpdate) {
			world.getComponentManager()->forEach<Components...>(
				[&](Entity e, QueryReference<Components>... comps) { mOnLateUpdate(e, comps..., dt); });
		}
	}

	void onDestroy(World& world) override {
		if (mOnDestroy) {
			world.getComponentManager()->forEach<Components...>(mOnDestroy);
		}
	}
};

template <typename... Components> class SystemBuilder {
	SystemManager& mManager;
	std::shared_ptr<MultiPhaseSystem<Components...>> mSystem;

public:
	SystemBuilder(SystemManager& manager, std::string name)
		: mManager(manager) {
		mSystem = std::make_shared<MultiPhaseSystem<Components...>>(name, manager);
		mManager.addSystem(mSystem);
	}

	// Fluent API for setting phase callbacks
	SystemBuilder& onAwake(typename MultiPhaseSystem<Components...>::InitFunc func) {
		mSystem->setOnAwake(std::move(func));
		return *this;
	}

	SystemBuilder& onStart(typename MultiPhaseSystem<Components...>::InitFunc func) {
		mSystem->setOnStart(std::move(func));
		return *this;
	}

	SystemBuilder& onUpdate(typename MultiPhaseSystem<Components...>::UpdateFunc func) {
		mSystem->setOnUpdate(std::move(func));
		return *this;
	}

	SystemBuilder& onFixedUpdate(typename MultiPhaseSystem<Components...>::UpdateFunc func) {
		mSystem->setOnFixedUpdate(std::move(func));
		return *this;
	}

	SystemBuilder& onLateUpdate(typename MultiPhaseSystem<Components...>::UpdateFunc func) {
		mSystem->setOnLateUpdate(std::move(func));
		return *this;
	}

	SystemBuilder& onDestroy(typename MultiPhaseSystem<Components...>::InitFunc func) {
		mSystem->setOnDestroy(std::move(func));
		return *this;
	}

	// Legacy support: each() maps to onUpdate
	SystemBuilder& each(typename MultiPhaseSystem<Components...>::UpdateFunc func) {
		return onUpdate(func);
	}

	// Get the system pointer
	std::shared_ptr<System> getSystem() {
		return mSystem;
	}
};
}
