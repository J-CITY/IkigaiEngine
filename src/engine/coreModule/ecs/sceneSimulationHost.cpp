#include "sceneSimulationHost.h"

#include "coreModule/ecs/componentTypes.h"
#include "coreModule/ecs/components/ambientSphereLight.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/directionalLight.h"
#include "coreModule/ecs/systems/environmentSystem.h"
#include "coreModule/ecs/systems/particleSimulationSystem.h"
#include "coreModule/ecs/systems/skeletalStateGraphSystem.h"
#include "coreModule/ecs/systems/surfaceSystem.h"
#include "coreModule/ecs/systems/variableContextSystem.h"
#include "sceneModule/scene.h"
#include "sceneModule/sceneSystem.h"
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
#if USE_AI
#include "coreModule/ecs/systems/behaviorTreeSystem.h"
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
		void registerSystems(SystemManager& systems) override;
		void setPhysicsFactories(PhysicsFactories physics) override;

#if USE_PHYSICS_3D
		PHYSICS::PhysicWorld& physics3d(Scene& scene) override;
		std::shared_ptr<PHYSICS::PhysicWorld> physics3dShared(Scene& scene) override;
		bool physics3dPaused() const override;
		void setPhysics3dPaused(Scene& scene, bool paused) override;
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
#ifdef USE_EDITOR
		std::optional<PhysicsDebugState<PHYSICS2D::World>> physics2dDebug() override;
#endif
#endif

	private:
		PhysicsFactories mPhysicsFactories;
#if USE_PHYSICS_3D
		bool physics3dWillStep() const;
		std::unique_ptr<Physics3DSystem> mPhysics3D;
#endif
#if USE_PHYSICS_2D
		std::unique_ptr<Physics2DSystem> mPhysics2D;
#endif
	};

#if USE_PHYSICS_3D || USE_PHYSICS_2D
	EngineSceneSimulation& engineSimulation(Scene& scene) {
		auto* simulation = dynamic_cast<EngineSceneSimulation*>(scene.simulation());
		if (!simulation) {
			throw std::logic_error("The scheduled scene phase requires the engine simulation");
		}
		return *simulation;
	}
#endif

	void registerAfter(SystemManager& systems, SystemPhase phase, const char* name, const char* predecessor, SceneSystemCallback callback) {
		RegisterSceneSystem(systems, phase, name, callback).runAfter(predecessor);
	}

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
	void EngineSceneSimulation::registerSystems(SystemManager& systems) {
		constexpr auto environmentStart = "Environment.Start";
		if (systems.getSystem<System>(environmentStart)) {
			return;
		}

		constexpr auto beforeObjects = SystemPhase::BeforeObjectUpdate;
		RegisterSceneSystem(systems, beforeObjects, "Surface.BeginFrame",
			[](const ScenePhaseContext& frame, CommandBuffer&) { SurfaceSystem::beginFrame(frame.scene); });
		registerAfter(systems, beforeObjects, "Environment.Update", "Surface.BeginFrame",
			[](const ScenePhaseContext& frame, CommandBuffer&) { EnvironmentSystem::update(frame.scene, frame.dt.count()); });
#if USE_PHYSICS_3D
		registerAfter(systems, beforeObjects, "Ragdoll.Update", "Environment.Update", [](const ScenePhaseContext& frame, CommandBuffer&) {
			RagdollSystem::update(frame.scene, std::clamp(float(frame.dt.count()), 0.f, .1f));
		});
		registerAfter(systems, beforeObjects, "Character.Update", "Ragdoll.Update", [](const ScenePhaseContext& frame, CommandBuffer&) {
			CharacterSystem::update(frame.scene, std::clamp(float(frame.dt.count()), 0.f, .1f));
		});
		registerAfter(systems, beforeObjects, "Scene.WaitForFrameHook", "Character.Update", [](const ScenePhaseContext& frame, CommandBuffer&) {
			if (TESTHOOK::waitForFrame) {
				TESTHOOK::waitForFrame(frame.scene);
			}
		});
#endif
		RegisterSceneSystem(systems, SystemPhase::AfterObjectUpdate, "Particles.Update",
			[](const ScenePhaseContext& frame, CommandBuffer&) { ParticleSimulationSystem::update(frame.scene, frame.dt.count()); });

		// Property bindings can target arbitrary components, so keep the fixed-step consumers ordered.
		constexpr auto finishFixed = SystemPhase::FinishFixedUpdate;
		const char* beforeVariables = nullptr;
#if USE_AI
		RegisterSceneSystem(systems, finishFixed, "BehaviorTree.ApplyCommands",
			[](const ScenePhaseContext& frame, CommandBuffer&) { BehaviorTreeSystem::applyCommands(frame.scene); });
		beforeVariables = "BehaviorTree.ApplyCommands";
#endif
#if USE_PHYSICS_3D
		if (beforeVariables) {
			registerAfter(systems, finishFixed, "Character.FixedUpdate", beforeVariables,
				[](const ScenePhaseContext& frame, CommandBuffer&) { CharacterSystem::fixedUpdate(frame.scene, float(frame.dt.count())); });
		}
		else {
			RegisterSceneSystem(systems, finishFixed, "Character.FixedUpdate",
				[](const ScenePhaseContext& frame, CommandBuffer&) { CharacterSystem::fixedUpdate(frame.scene, float(frame.dt.count())); });
		}
		beforeVariables = "Character.FixedUpdate";
#endif
		if (beforeVariables) {
			registerAfter(systems, finishFixed, "VariableContext.Update", beforeVariables,
				[](const ScenePhaseContext& frame, CommandBuffer&) { VariableContextSystem::update(frame.scene); });
		}
		else {
			RegisterSceneSystem(systems, finishFixed, "VariableContext.Update",
				[](const ScenePhaseContext& frame, CommandBuffer&) { VariableContextSystem::update(frame.scene); });
		}
#if USE_AI
		registerAfter(systems, finishFixed, "BehaviorTree.Update", "VariableContext.Update",
			[](const ScenePhaseContext& frame, CommandBuffer&) { BehaviorTreeSystem::update(frame.scene, frame.dt.count()); });
#endif
		registerAfter(systems, finishFixed, "SkeletalStateGraph.Update",
#if USE_AI
			"BehaviorTree.Update",
#else
			"VariableContext.Update",
#endif
			[](const ScenePhaseContext& frame, CommandBuffer&) { SkeletalStateGraphSystem::update(frame.scene, float(frame.dt.count())); });
#if USE_PHYSICS_3D
		registerAfter(systems, finishFixed, "Ragdoll.AfterAnimation", "SkeletalStateGraph.Update",
			[](const ScenePhaseContext& frame, CommandBuffer&) { RagdollSystem::afterAnimation(frame.scene, float(frame.dt.count())); });
		registerAfter(systems, finishFixed, "Scene.AfterAnimationHook", "Ragdoll.AfterAnimation", [](const ScenePhaseContext& frame, CommandBuffer&) {
			if (TESTHOOK::afterAnimation) {
				TESTHOOK::afterAnimation(frame.scene, float(frame.dt.count()));
			}
		});

		RegisterSceneSystem(systems, SystemPhase::BeginFrame, "Destruction.BeginFrame",
			[](const ScenePhaseContext& frame, CommandBuffer&) { DestructionSystem::beginFrame(frame.scene); });
		constexpr auto physics3dPhase = SystemPhase::Physics3D;
		RegisterSceneSystem(systems, physics3dPhase, "Destruction.BeforePhysics", [](const ScenePhaseContext& frame, CommandBuffer&) {
			if (engineSimulation(frame.scene).physics3dWillStep()) {
				DestructionSystem::beforePhysics(frame.scene, float(frame.dt.count()));
			}
		});
		registerAfter(
			systems, physics3dPhase, "Ragdoll.BeforePhysics", "Destruction.BeforePhysics", [](const ScenePhaseContext& frame, CommandBuffer&) {
				if (engineSimulation(frame.scene).physics3dWillStep()) {
					RagdollSystem::beforePhysics(frame.scene, float(frame.dt.count()));
				}
			});
		registerAfter(
			systems, physics3dPhase, "Physics3D.Step", "Ragdoll.BeforePhysics", [](const ScenePhaseContext& frame, CommandBuffer& commands) {
				engineSimulation(frame.scene).mPhysics3D->onFixedUpdate(frame.scene.getWorld(), commands, frame.dt);
			});
		registerAfter(systems, physics3dPhase, "Ragdoll.AfterPhysics", "Physics3D.Step", [](const ScenePhaseContext& frame, CommandBuffer&) {
			if (engineSimulation(frame.scene).mPhysics3D->lastStepSucceeded()) {
				RagdollSystem::afterPhysics(frame.scene);
			}
		});
		registerAfter(
			systems, physics3dPhase, "Destruction.AfterPhysics", "Ragdoll.AfterPhysics", [](const ScenePhaseContext& frame, CommandBuffer&) {
				if (engineSimulation(frame.scene).mPhysics3D->lastStepSucceeded()) {
					DestructionSystem::afterPhysics(frame.scene, float(frame.dt.count()));
				}
			});
		RegisterSceneSystem(systems, SystemPhase::Present, "Destruction.Present", [](const ScenePhaseContext& frame, CommandBuffer&) {
			DestructionSystem::present(frame.scene, frame.scene.physics3dPaused() ? 1.f : frame.interpolationAlpha);
		});
#endif
#if USE_PHYSICS_2D
		RegisterSceneSystem(systems, SystemPhase::Physics2D, "Physics2D.Step", [](const ScenePhaseContext& frame, CommandBuffer& commands) {
			engineSimulation(frame.scene).mPhysics2D->onFixedUpdate(frame.scene.getWorld(), commands, frame.dt);
		});
#endif
		// Resolve the environment's typed light reference before scene loading reports success.
		RegisterSceneSystem(systems, SystemPhase::SceneStart, environmentStart, [](const ScenePhaseContext& frame, CommandBuffer&) {
			if (auto* environment = EnvironmentSystem::active(frame.scene)) {
				environment->update(frame.scene, 0.0);
			}
		});
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
	bool EngineSceneSimulation::physics3dWillStep() const {
		return !mPhysics3D->paused || mPhysics3D->singleStep;
	}
	bool EngineSceneSimulation::physics3dPaused() const {
		return mPhysics3D && mPhysics3D->paused;
	}
	void EngineSceneSimulation::setPhysics3dPaused(Scene& scene, bool paused) {
		physics3d(scene);
		mPhysics3D->paused = paused;
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
#ifdef USE_EDITOR
	std::optional<PhysicsDebugState<PHYSICS2D::World>> EngineSceneSimulation::physics2dDebug() {
		if (!mPhysics2D) {
			return std::nullopt;
		}
		return PhysicsDebugState<PHYSICS2D::World> { mPhysics2D->simulation(), mPhysics2D->paused, mPhysics2D->singleStep };
	}
#endif
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
