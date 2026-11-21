#include "sceneSimulationHost.h"

#include "coreModule/ecs/componentTypes.h"
#include "coreModule/ecs/components/ambientSphereLight.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/directionalLight.h"
#include "coreModule/ecs/systems/behaviorTreeSystem.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "coreModule/ecs/systems/skeletalStateGraphSystem.h"
#include "coreModule/ecs/systems/surfaceSystem.h"
#include "coreModule/ecs/systems/variableContextSystem.h"
#include "sceneModule/scene.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/systems/characterSystem.h"
#include "coreModule/ecs/systems/destructionSystem.h"
#include "coreModule/ecs/systems/physics3dSystem.h"
#include "coreModule/ecs/systems/ragdollSystem.h"
#include "coreModule/testHooks.h"
#include "destructionModule/destructionRuntime.h"
#endif
#if USE_PHYSICS_2D
#include "coreModule/ecs/systems/physics2dSystem.h"
#endif
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace IKIGAI::ECS {
namespace {
	using namespace SCENE_SYSTEM;

	class EngineSceneSimulation final : public SceneSimulation {
	public:
		explicit EngineSceneSimulation(PhysicsFactories physics)
			: mPhysicsFactories(std::move(physics)) { }

		void populateDefaultScene(Scene& scene) override;
		void afterStart(Scene& scene) override;
		void beginFrame(Scene& scene) override;
		void beforeObjectUpdate(Scene& scene, std::chrono::duration<double> dt) override;
		void afterObjectUpdate(Scene& scene, std::chrono::duration<double> dt) override;
		void finishFixedUpdate(Scene& scene, std::chrono::duration<double> dt) override;
		void setPhysicsFactories(PhysicsFactories physics) override;

#if USE_PHYSICS_3D
		PHYSICS::PhysicWorld& physics3d(Scene& scene) override;
		std::shared_ptr<PHYSICS::PhysicWorld> physics3dShared(Scene& scene) override;
		void stepPhysics3D(Scene& scene, std::chrono::duration<double> dt, bool beginDestructionFrame) override;
		bool physics3dPaused() const override;
		void setPhysics3dPaused(Scene& scene, bool paused) override;
		void presentDestructibles(Scene& scene, float alpha) override;
#ifdef USE_EDITOR
		std::optional<PhysicsDebugState<PHYSICS::PhysicWorld>> physics3dDebug() override;
#endif
		void setDestructionSettings(const DESTRUCTION::RuntimeSettings& settings) override;
		const DESTRUCTION::RuntimeSettings& getDestructionSettings() const override;
		DESTRUCTION::RuntimeSettings destructionSettings {};
		std::shared_ptr<DESTRUCTION::DestructionRuntime> destructionRuntime {};
#endif
#if USE_PHYSICS_2D
		PHYSICS2D::World& physics2d(Scene& scene) override;
		void stepPhysics2D(Scene& scene, std::chrono::duration<double> dt) override;
#ifdef USE_EDITOR
		std::optional<PhysicsDebugState<PHYSICS2D::World>> physics2dDebug() override;
#endif
#endif

	private:
		PhysicsFactories mPhysicsFactories;
#if USE_PHYSICS_3D
		std::unique_ptr<Physics3DSystem> mPhysics3D;
#endif
#if USE_PHYSICS_2D
		std::unique_ptr<Physics2DSystem> mPhysics2D;
#endif
	};

#if USE_PHYSICS_3D
	void EngineSceneSimulation::setDestructionSettings(const DESTRUCTION::RuntimeSettings& settings) {
		settings.validate();
		destructionSettings = settings;
	}
	const DESTRUCTION::RuntimeSettings& EngineSceneSimulation::getDestructionSettings() const {
		return destructionSettings;
	}
#endif
	void EngineSceneSimulation::populateDefaultScene(Scene& scene) {
		auto directionalLight = scene.createObject("Directional Light");
		directionalLight->addComponent<ECS::DirectionalLight>()->setIntensity(0.75f);
		directionalLight->getTransform()->setLocalPosition({ 0.0f, 10.0f, 0.0f });
		directionalLight->getTransform()->setLocalRotation(MATH::QuaternionF(MATH::Vector3 { 120.0f, -40.0f, 0.0f }));

		auto ambientLight = scene.createObject("Ambient Light");
		ambientLight->addComponent<ECS::AmbientSphereLight>()->setRadius(10000.0f);

		auto camera = scene.createObject("Main Camera");
		camera->addComponent<ECS::CameraComponent>();
		camera->getTransform()->setLocalRotation(MATH::QuaternionF(MATH::Vector3f { 20.0f, 180.0f, 0.0f }));
		camera->getTransform()->setLocalPosition({ 0.0f, 3.0f, 8.0f });
	}
	void EngineSceneSimulation::afterStart(Scene& scene) {
		// Resolve the environment's typed light reference before scene loading reports success.
		if (auto* environment = ECS::EnvironmentSystem::active(scene)) {
			environment->update(scene, 0.0);
		}
	}
	void EngineSceneSimulation::beginFrame([[maybe_unused]] Scene& scene) {
#if USE_PHYSICS_3D
		ECS::DestructionSystem::beginFrame(scene);
#endif
	}
	void EngineSceneSimulation::beforeObjectUpdate(Scene& scene, std::chrono::duration<double> dt) {
		ECS::SurfaceSystem::beginFrame(scene);
		ECS::EnvironmentSystem::update(scene, dt.count());
#if USE_PHYSICS_3D
		ECS::RagdollSystem::update(scene, std::clamp(float(dt.count()), 0.f, .1f));
		ECS::CharacterSystem::update(scene, std::clamp(float(dt.count()), 0.f, .1f));
		if (TESTHOOK::waitForFrame) {
			TESTHOOK::waitForFrame(scene);
		}
#endif
	}
	void EngineSceneSimulation::afterObjectUpdate(Scene& scene, std::chrono::duration<double> dt) {
		// A scene owns its particle instances through its objects. No render pass ticks them.
		for (auto& o : scene.getObjects()) {
			if (o->getIsActive()) {
				if (auto c = o->getComponent<ECS::ParticleSystem2DComponent>()) {
					c->tick(dt.count());
				}
				if (auto c = o->getComponent<ECS::ParticleSystem3DComponent>()) {
					c->tick(dt.count());
				}
				if (auto c = o->getComponent<ECS::SpriteParticleComponent>()) {
					c->tick(dt.count());
				}
			}
		}
	}
	void EngineSceneSimulation::finishFixedUpdate(Scene& scene, std::chrono::duration<double> dt) {
		// Called after ECS producers and physics have completed for this tick.
		// Scene-local ordered systems. Bound properties may target any component;
		// these phases deliberately run serially, after producers and before consumers.
		ECS::BehaviorTreeSystem::applyCommands(scene);
#if USE_PHYSICS_3D
		ECS::CharacterSystem::fixedUpdate(scene, float(dt.count()));
#endif
		ECS::VariableContextSystem::update(scene);
		ECS::BehaviorTreeSystem::update(scene, dt.count());
		ECS::SkeletalStateGraphSystem::update(scene, float(dt.count()));
#if USE_PHYSICS_3D
		ECS::RagdollSystem::afterAnimation(scene, float(dt.count()));
#endif
#if USE_PHYSICS_3D
		if (TESTHOOK::afterAnimation) {
			TESTHOOK::afterAnimation(scene, float(dt.count()));
		}
#endif
	}
	void EngineSceneSimulation::setPhysicsFactories(PhysicsFactories physics) {
#if USE_PHYSICS_2D
		if (mPhysics2D) {
			throw std::logic_error("Configure physics before creating the scene's world");
		}
#endif
#if USE_PHYSICS_3D
		if (mPhysics3D) {
			throw std::logic_error("Configure physics before creating the scene's world");
		}
#endif
		mPhysicsFactories = std::move(physics);
	}
#if USE_PHYSICS_3D
	PHYSICS::PhysicWorld& EngineSceneSimulation::physics3d(Scene& scene) {
		if (!mPhysics3D) {
			mPhysics3D = std::make_unique<ECS::Physics3DSystem>(
				scene, mPhysicsFactories.physics3d ? mPhysicsFactories.physics3d() : std::make_unique<PHYSICS::World>());
		}
		return mPhysics3D->simulation();
	}
	void EngineSceneSimulation::stepPhysics3D(Scene& scene, std::chrono::duration<double> dt, bool beginDestructionFrame) {
		physics3d(scene);
		if (beginDestructionFrame) {
			ECS::DestructionSystem::beginFrame(scene);
		}
		const bool destructionStep = !mPhysics3D->paused || mPhysics3D->singleStep;
		if (destructionStep) {
			ECS::DestructionSystem::beforePhysics(scene, float(dt.count()));
			ECS::RagdollSystem::beforePhysics(scene, float(dt.count()));
		}
		ECS::CommandBuffer commands;
		mPhysics3D->onFixedUpdate(scene.getWorld(), commands, dt);
		commands.execute();
		if (mPhysics3D->lastStepSucceeded()) {
			ECS::RagdollSystem::afterPhysics(scene);
			ECS::DestructionSystem::afterPhysics(scene, float(dt.count()));
		}
		if (beginDestructionFrame) {
			presentDestructibles(scene, 1);
		}
	}
	bool EngineSceneSimulation::physics3dPaused() const {
		return mPhysics3D && mPhysics3D->paused;
	}
	void EngineSceneSimulation::setPhysics3dPaused(Scene& scene, bool paused) {
		physics3d(scene);
		mPhysics3D->paused = paused;
	}
	void EngineSceneSimulation::presentDestructibles(Scene& scene, float alpha) {
		ECS::DestructionSystem::present(scene, mPhysics3D && mPhysics3D->paused ? 1.f : alpha);
	}
	std::shared_ptr<PHYSICS::PhysicWorld> EngineSceneSimulation::physics3dShared(Scene& scene) {
		physics3d(scene);
		return mPhysics3D->sharedSimulation();
	}
#ifdef USE_EDITOR
	std::optional<PhysicsDebugState<PHYSICS::PhysicWorld>> EngineSceneSimulation::physics3dDebug() {
		if (!mPhysics3D) {
			return std::nullopt;
		}
		return PhysicsDebugState<PHYSICS::PhysicWorld> { mPhysics3D->simulation(), mPhysics3D->paused, mPhysics3D->singleStep };
	}
#endif
#endif
#if USE_PHYSICS_2D
	PHYSICS2D::World& EngineSceneSimulation::physics2d(Scene& scene) {
		if (!mPhysics2D) {
			mPhysics2D = std::make_unique<ECS::Physics2DSystem>(
				scene, mPhysicsFactories.physics2d ? mPhysicsFactories.physics2d() : std::make_unique<PHYSICS2D::World>());
		}
		return mPhysics2D->simulation();
	}
	void EngineSceneSimulation::stepPhysics2D(Scene& scene, std::chrono::duration<double> dt) {
		physics2d(scene);
		ECS::CommandBuffer commands;
		mPhysics2D->onFixedUpdate(scene.getWorld(), commands, dt);
		commands.execute();
	}
#ifdef USE_EDITOR
	std::optional<PhysicsDebugState<PHYSICS2D::World>> EngineSceneSimulation::physics2dDebug() {
		if (!mPhysics2D) {
			return std::nullopt;
		}
		return PhysicsDebugState<PHYSICS2D::World> { mPhysics2D->simulation(), mPhysics2D->paused, mPhysics2D->singleStep };
	}
#endif
#endif

#if USE_PHYSICS_3D
	EngineSceneSimulation& engineSimulation(Scene& scene) {
		auto* simulation = dynamic_cast<EngineSceneSimulation*>(scene.simulation());
		if (!simulation) {
			throw std::logic_error("Destruction requires the engine scene simulation");
		}
		return *simulation;
	}
#endif
}

std::unique_ptr<SCENE_SYSTEM::SceneSimulation> CreateSceneSimulation(SCENE_SYSTEM::PhysicsFactories physics) {
	return std::make_unique<EngineSceneSimulation>(std::move(physics));
}
#if USE_PHYSICS_3D
DESTRUCTION::RuntimeSettings& SceneDestructionSettings(SCENE_SYSTEM::Scene& scene) {
	return engineSimulation(scene).destructionSettings;
}
std::shared_ptr<DESTRUCTION::DestructionRuntime>& SceneDestructionRuntime(SCENE_SYSTEM::Scene& scene) {
	return engineSimulation(scene).destructionRuntime;
}
#endif
}
