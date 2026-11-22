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
	template <typename Callback> using CallbackSlot = std::shared_ptr<const Callback>;

	SystemManager& mManager;
	CallbackSlot<InitFunc> mOnAwake {};
	CallbackSlot<InitFunc> mOnStart {};
	CallbackSlot<UpdateFunc> mOnUpdate {};
	CallbackSlot<UpdateFunc> mOnFixedUpdate {};
	CallbackSlot<UpdateFunc> mOnLateUpdate {};
	CallbackSlot<InitFunc> mOnDestroy {};

	template <typename Callback> void setCallback(SystemPhase phase, CallbackSlot<Callback>& callback, Callback&& replacement) {
		std::shared_ptr<const Callback> previous;
		const auto lock = mManager.lockScheduleForChange();
		const bool enabled = static_cast<bool>(replacement);
		previous = std::exchange(callback, enabled ? std::make_shared<const Callback>(std::move(replacement)) : nullptr);
		if (static_cast<bool>(previous) != enabled) {
			setPhaseEnabled(phase, enabled);
			mManager.mIsDirty = true;
		}
	}

	template <typename Callback> CallbackSlot<Callback> snapshotCallback(const CallbackSlot<Callback>& slot) const {
		std::lock_guard lock(mManager.mScheduleMutex);
		return slot;
	}

	void runUpdateCallback(World& world, const CallbackSlot<UpdateFunc>& slot, std::chrono::duration<double> dt) {
		// A callback can replace itself. All entities in this invocation use the same callable.
		if (const auto callback = snapshotCallback(slot)) {
			world.getComponentManager()->forEach<Components...>(
				[&](Entity entity, QueryReference<Components>... components) { (*callback)(entity, components..., dt); });
		}
	}

public:
	MultiPhaseSystem(const std::string& name, SystemManager& mgr)
		: mManager(mgr) {
		mName = name;
		setPhases({});
		auto& cm = *mgr.getWorld().getComponentManager();
		// Populate dependency tracking
		((AccessTraits<Components>::ReadOnly ? mReads.insert(cm.getComponentType<Components>()) : mWrites.insert(cm.getComponentType<Components>())),
			...);
	}

	// Setters for each phase
	void setOnAwake(InitFunc&& f) {
		setCallback(SystemPhase::Awake, mOnAwake, std::move(f));
	}
	void setOnStart(InitFunc&& f) {
		setCallback(SystemPhase::Start, mOnStart, std::move(f));
	}
	void setOnUpdate(UpdateFunc&& f) {
		setCallback(SystemPhase::Update, mOnUpdate, std::move(f));
	}
	void setOnFixedUpdate(UpdateFunc&& f) {
		setCallback(SystemPhase::FixedUpdate, mOnFixedUpdate, std::move(f));
	}
	void setOnLateUpdate(UpdateFunc&& f) {
		setCallback(SystemPhase::LateUpdate, mOnLateUpdate, std::move(f));
	}
	void setOnDestroy(InitFunc&& f) {
		setCallback(SystemPhase::Destroy, mOnDestroy, std::move(f));
	}

	// System interface implementations
	void onAwake(World& world) override {
		if (const auto callback = snapshotCallback(mOnAwake)) {
			world.getComponentManager()->forEach<Components...>(*callback);
		}
	}

	void onStart(World& world) override {
		if (const auto callback = snapshotCallback(mOnStart)) {
			world.getComponentManager()->forEach<Components...>(*callback);
		}
	}

	void onUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override {
		runUpdateCallback(world, mOnUpdate, dt);
	}

	void onFixedUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override {
		runUpdateCallback(world, mOnFixedUpdate, dt);
	}

	void onLateUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override {
		runUpdateCallback(world, mOnLateUpdate, dt);
	}

	void onDestroy(World& world) override {
		if (const auto callback = snapshotCallback(mOnDestroy)) {
			world.getComponentManager()->forEach<Components...>(*callback);
		}
	}
};

template <typename... Components> class SystemBuilder {
	std::shared_ptr<MultiPhaseSystem<Components...>> mSystem;

public:
	SystemBuilder(SystemManager& manager, std::string name)
		: mSystem(std::make_shared<MultiPhaseSystem<Components...>>(name, manager)) {
		manager.addSystem(mSystem);
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
