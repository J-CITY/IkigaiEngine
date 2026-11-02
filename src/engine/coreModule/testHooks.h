#pragma once

#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/traversalComponent.h"
#include "skeletalModule/animation.h"
#include "skeletalModule/stateGraphIr.h"

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

// Production calls these when a test module assigned them. They stay null in a
// normal build, so the engine does not include test headers.
namespace IKIGAI::TESTHOOK {
inline void (*waitForFrame)(SCENE_SYSTEM::Scene&) = nullptr;
inline void (*afterAnimation)(SCENE_SYSTEM::Scene&, float) = nullptr;
inline bool (*characterBeforeMove)(SCENE_SYSTEM::Scene&, ECS::CharacterMotorComponent&, ECS::TraversalComponent&, float) = nullptr;
inline void (*characterAfterAttempt)(SCENE_SYSTEM::Scene&, ECS::CharacterMotorComponent&, ECS::TraversalComponent&) = nullptr;
inline void (*traversalContact)(
	SCENE_SYSTEM::Scene&, ECS::CharacterMotorComponent&, ECS::TraversalComponent&, const MATH::Vector3f&, const SKELETON::Pose&, float)
	= nullptr;
inline void (*stateGraphReloaded)(const std::string&) = nullptr;
inline void (*animationEvent)(const std::string& clip, const std::string& name, float timeSeconds) = nullptr;
inline void (*rootMotionSampled)(const SKELETON::RootMotionSample&) = nullptr;
inline void (*focusPresentation)(void*) = nullptr;
}
