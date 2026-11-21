#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace IKIGAI::DESTRUCTION {
struct RuntimeSettings;
}

namespace IKIGAI::PHYSICS {
class PhysicWorld;
}
namespace IKIGAI::PHYSICS2D {
class World;
}

namespace IKIGAI::SCENE_SYSTEM {
class Scene;

struct PhysicsFactories {
#if USE_PHYSICS_2D
	std::function<std::unique_ptr<PHYSICS2D::World>()> physics2d;
#endif
#if USE_PHYSICS_3D
	std::function<std::unique_ptr<PHYSICS::PhysicWorld>()> physics3d;
#endif
};

#ifdef USE_EDITOR
template <class World> struct PhysicsDebugState {
	World& world;
	bool& paused;
	bool& singleStep;
};
#endif

// Each scene owns its integration, including while an asynchronous load is pending.
class SceneSimulation {
public:
	virtual ~SceneSimulation() = default;
	virtual void populateDefaultScene(Scene& scene) = 0;
	virtual void afterStart(Scene& scene) = 0;
	virtual void beginFrame(Scene& scene) = 0;
	virtual void beforeObjectUpdate(Scene& scene, std::chrono::duration<double> dt) = 0;
	virtual void afterObjectUpdate(Scene& scene, std::chrono::duration<double> dt) = 0;
	virtual void finishFixedUpdate(Scene& scene, std::chrono::duration<double> dt) = 0;
	virtual void setPhysicsFactories(PhysicsFactories physics) = 0;
#if USE_PHYSICS_3D
	virtual void setDestructionSettings(const DESTRUCTION::RuntimeSettings& settings) = 0;
	virtual const DESTRUCTION::RuntimeSettings& getDestructionSettings() const = 0;
	virtual PHYSICS::PhysicWorld& physics3d(Scene& scene) = 0;
	virtual std::shared_ptr<PHYSICS::PhysicWorld> physics3dShared(Scene& scene) = 0;
	virtual void stepPhysics3D(Scene& scene, std::chrono::duration<double> dt, bool beginDestructionFrame) = 0;
	virtual bool physics3dPaused() const = 0;
	virtual void setPhysics3dPaused(Scene& scene, bool paused) = 0;
	virtual void presentDestructibles(Scene& scene, float alpha) = 0;
#ifdef USE_EDITOR
	virtual std::optional<PhysicsDebugState<PHYSICS::PhysicWorld>> physics3dDebug() = 0;
#endif
#endif
#if USE_PHYSICS_2D
	virtual PHYSICS2D::World& physics2d(Scene& scene) = 0;
	virtual void stepPhysics2D(Scene& scene, std::chrono::duration<double> dt) = 0;
#ifdef USE_EDITOR
	virtual std::optional<PhysicsDebugState<PHYSICS2D::World>> physics2dDebug() = 0;
#endif
#endif
};

using SceneSimulationFactory = std::unique_ptr<SceneSimulation> (*)(PhysicsFactories);
}
