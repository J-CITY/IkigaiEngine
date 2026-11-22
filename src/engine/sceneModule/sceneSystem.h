#pragma once

#include "ecsModule/systemManager.h"

namespace IKIGAI::SCENE_SYSTEM {
class Scene;

// Borrowed by the schedule for one synchronous scene phase.
struct ScenePhaseContext final : ECS::SystemPhaseContext {
	ScenePhaseContext(Scene& scene, std::chrono::duration<double> deltaTime, float alpha)
		: scene(scene)
		, dt(deltaTime)
		, interpolationAlpha(alpha) { }

	Scene& scene;
	std::chrono::duration<double> dt;
	float interpolationAlpha;
};

using SceneSystemCallback = void (*)(const ScenePhaseContext&, ECS::CommandBuffer&);

ECS::System& RegisterSceneSystem(ECS::SystemManager& systems, ECS::SystemPhase phase, const char* name, SceneSystemCallback callback);
}
