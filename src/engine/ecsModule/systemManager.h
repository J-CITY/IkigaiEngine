#pragma once
#include "access.h"
#include "commandBuffer.h"
#include "componentManager.h"
#include "entityManager.h"
#include "signature.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace IKIGAI::ECS {

class World;

// Access Types for dependency graph
// Defined in access.h

class System {
public:
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

	const std::string& getName() const {
		return mName;
	}
	void setName(const std::string& name) {
		mName = name;
	}

	// Metadata for Graph
	std::string mName;
	std::unordered_set<ComponentType> mReads;
	std::unordered_set<ComponentType> mWrites;
	// Lower values run earlier. Equal values keep registration order.
	int mPriority = 0;
	// Never shares a batch, including with systems whose components do not overlap.
	bool mExclusiveBatch = false;

	// Signature for matching (if Query based)
	Signature mQueryHas;
	Signature mQueryNot;

	// State flags
	bool mIsAwake = false;
	bool mIsStarted = false;
};

class SystemManager {
public:
	struct Batch {
		std::vector<std::shared_ptr<System>> systems;
	};

	SystemManager(World& world)
		: mWorld(world) { }

	template <typename T, typename... Args> std::shared_ptr<T> registerSystem(Args&&... args) {
		auto system = std::make_shared<T>(std::forward<Args>(args)...);
		const auto& name = system->getName();
		if (const auto existing = mSystems.find(name); existing != mSystems.end()) {
			const auto orderIt = std::find_if(
				mSystemsOrder.begin(), mSystemsOrder.end(), [&name](const std::shared_ptr<System>& entry) { return entry->getName() == name; });
			if (orderIt != mSystemsOrder.end()) {
				*orderIt = system;
			}
		}
		else {
			mSystemsOrder.push_back(system);
		}
		mSystems[name] = system;
		mIsDirty = true;
		return system;
	}

	void addSystem(std::shared_ptr<System> system) {
		const auto& name = system->getName();
		if (const auto existing = mSystems.find(name); existing != mSystems.end()) {
			const auto orderIt = std::find_if(
				mSystemsOrder.begin(), mSystemsOrder.end(), [&name](const std::shared_ptr<System>& entry) { return entry->getName() == name; });
			if (orderIt != mSystemsOrder.end()) {
				*orderIt = system;
			}
		}
		else {
			mSystemsOrder.push_back(system);
		}
		mSystems[name] = system;
		mIsDirty = true;
	}

	[[nodiscard]] ComponentManager* getComponentManager() const noexcept;

	// Phase execution methods
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
		if (mSystems.contains(name)) {
			return std::static_pointer_cast<T>(mSystems.at(name));
		}
		return nullptr;
	}

private:
	World& mWorld;
	std::unordered_map<std::string, std::shared_ptr<System>> mSystems;
	std::vector<std::shared_ptr<System>> mSystemsOrder;
	std::vector<Batch> mBatches;
	bool mIsDirty = true;

	void rebuildBatches();

	// Dependency Graph
	// std::vector<std::vector<int>> mGraph;
	// void buildGraph();
};
}
