#ifdef USE_EDITOR
#include "componentEditorHooks.h"
#if USE_PHYSICS_2D
#include "coreModule/ecs/components/cloth2dComponent.h"
#include "coreModule/ecs/components/forceField2dComponent.h"
#include "coreModule/ecs/components/physics2dComponent.h"
#endif
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/capsuleColliderComponent.h"
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/destructibleMeshComponent.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/physicsComponent.h"
#include "coreModule/ecs/components/ragdollComponent.h"
#include "coreModule/ecs/components/thirdPersonCameraComponent.h"
#include "coreModule/ecs/components/thirdPersonController.h"
#include "coreModule/ecs/components/traversalComponent.h"
#endif

namespace IKIGAI::EDITOR::detail {
void registerPhysicsComponentEditors(ComponentEditorRegistry& registry) {
#if USE_PHYSICS_2D
	addComponentEditorHooks<ECS::Cloth2DComponent, ECS::ForceField2DComponent, ECS::Physics2DComponent, ECS::SoftBody2DComponent,
		ECS::Fluid2DComponent, ECS::Joint2DComponent>(registry);
#endif
#if USE_PHYSICS_3D
	addComponentEditorHooks<ECS::CapsuleColliderComponent, ECS::CharacterMotorComponent, ECS::DestructibleMeshComponent, ECS::Physics3DComponent,
		ECS::SoftBody3DComponent, ECS::Cloth3DComponent, ECS::Fluid3DComponent, ECS::ForceField3DComponent, ECS::PhysicsComponent,
		ECS::RagdollComponent, ECS::ThirdPersonCameraComponent, ECS::ThirdPersonController, ECS::TraversalComponent>(registry);
#endif
}
}
#endif
