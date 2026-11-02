#pragma once

// Component types are no longer enumerated here. Call ComponentCatalog::enroll<T>()
// from the component's .cpp. This header remains an include umbrella for existing
// translation units.

#include "components/ambientLight.h"
#include "components/ambientSphereLight.h"
#include "components/audioComponent.h"
#include "components/behaviorTreeComponent.h"
#include "components/blackboardComponent.h"
#include "components/cameraComponent.h"
#include "components/directionalLight.h"
#include "components/environmentComponent.h"
#include "components/facialAnimationComponent.h"
#include "components/foliageComponent.h"
#include "components/ikigaiScriptComponent.h"
#include "components/inputComponent.h"
#include "components/logicComponent.h"
#include "components/materialRenderer.h"
#include "components/modelRenderer.h"
#include "components/particleSystemComponent.h"
#include "components/pointLight.h"
#include "components/renderTargetComponent.h"
#include "components/scriptComponent.h"
#include "components/skeletalAnimationComponent.h"
#include "components/skeletalComponent.h"
#include "components/skeletalStateGraphComponent.h"
#include "components/surfaceComponent.h"
#include "components/transform.h"
#include "components/variableContextComponent.h"
#include "components/vegetationInteractorComponent.h"

#include "components/spotLight.h"
#if USE_PHYSICS_3D
#include "components/capsuleColliderComponent.h"
#include "components/characterMotorComponent.h"
#include "components/destructibleMeshComponent.h"
#include "components/physics3dComponent.h"
#include "components/physicsComponent.h"
#include "components/ragdollComponent.h"
#include "components/thirdPersonCameraComponent.h"
#include "components/thirdPersonController.h"
#include "components/traversalComponent.h"
#include "systems/characterSystem.h"
#endif
#if USE_PHYSICS_2D
#include "components/cloth2dComponent.h"
#include "components/forceField2dComponent.h"
#include "components/physics2dComponent.h"
#endif
#include "../gui/components/spriteComponent.h"
#include "../gui/components/vectorComponents.h"
#include "components/batchComponent.h"
