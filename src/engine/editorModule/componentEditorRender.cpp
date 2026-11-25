#ifdef USE_EDITOR
#include "componentEditorHooks.h"
#include "coreModule/ecs/components/ambientLight.h"
#include "coreModule/ecs/components/ambientSphereLight.h"
#include "coreModule/ecs/components/batchComponent.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/chunkModelRenderer.h"
#include "coreModule/ecs/components/directionalLight.h"
#include "coreModule/ecs/components/environmentComponent.h"
#include "coreModule/ecs/components/facialAnimationComponent.h"
#include "coreModule/ecs/components/foliageComponent.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coreModule/ecs/components/pointLight.h"
#include "coreModule/ecs/components/renderTargetComponent.h"
#include "coreModule/ecs/components/skeletalAnimationComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "coreModule/ecs/components/skeletalStateGraphComponent.h"
#include "coreModule/ecs/components/spotLight.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "coreModule/ecs/components/transform.h"
#include "coreModule/ecs/components/vegetationInteractorComponent.h"
#include "coreModule/ecs/components/virtualMeshRenderer.h"
#include "coreModule/ecs/components/waterComponent.h"

namespace IKIGAI::EDITOR::detail {
void registerRenderComponentEditors(ComponentEditorRegistry& registry) {
	addComponentEditorHooks<ECS::AmbientLight, ECS::AmbientSphereLight, ECS::BatchComponent, ECS::CameraComponent, ECS::VrCameraComponent,
		ECS::ChunkModelRenderer, ECS::DirectionalLight, ECS::EnvironmentComponent, ECS::FacialAnimationComponent, ECS::FoliageComponent,
		ECS::MaterialRenderer, ECS::ModelRenderer, ECS::ModelLODRenderer, ECS::ParticleSystem2DComponent, ECS::ParticleSystem3DComponent,
		ECS::PointLight, ECS::RenderTargetComponent, ECS::SkeletalAnimationComponent, ECS::SkeletalComponent, ECS::SkeletalStateGraphComponent,
		ECS::SpotLight, ECS::SurfaceComponent, ECS::WaterComponent, ECS::TransformComponent, ECS::VegetationInteractorComponent,
		ECS::VirtualMeshRenderer>(registry);
}
}
#endif
