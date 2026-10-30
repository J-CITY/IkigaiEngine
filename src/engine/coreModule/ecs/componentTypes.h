#pragma once

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
#include "utilsModule/serialization/typedDescriptor.h"
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <unordered_map>

#if USE_LUA
#define IKIGAI_LUA_COMPONENT ScriptComponent,
#define IKIGAI_LUA_DESCRIPTOR ScriptComponent::Descriptor,
#else
#define IKIGAI_LUA_COMPONENT
#define IKIGAI_LUA_DESCRIPTOR
#endif
#if USE_IKIGAI_SCRIPT
#define IKIGAI_SCRIPT_COMPONENT IkigaiScriptComponent,
#define IKIGAI_SCRIPT_DESCRIPTOR IkigaiScriptComponent::Descriptor,
#else
#define IKIGAI_SCRIPT_COMPONENT
#define IKIGAI_SCRIPT_DESCRIPTOR
#endif
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

#define IKIGAI_PHYSICS3D_COMPONENTS                                                                                                                  \
	Physics3DComponent, SoftBody3DComponent, Cloth3DComponent, Fluid3DComponent, ForceField3DComponent, ThirdPersonController,                       \
		CapsuleColliderComponent, CharacterMotorComponent, ThirdPersonCameraComponent, TraversalComponent, RagdollComponent,                         \
		DestructibleMeshComponent,
#define IKIGAI_PHYSICS3D_DESCRIPTORS                                                                                                                 \
	Physics3DComponent::Descriptor, SoftBody3DComponent::Descriptor, Cloth3DComponent::Descriptor, Fluid3DComponent::Descriptor,                     \
		ForceField3DComponent::Descriptor, ThirdPersonController::Descriptor, CapsuleColliderComponent::Descriptor,                                  \
		CharacterMotorComponent::Descriptor, ThirdPersonCameraComponent::Descriptor, TraversalComponent::Descriptor, RagdollComponent::Descriptor,   \
		DestructibleMeshComponent::Descriptor,
#define IKIGAI_PHYSICS3D_LEGACY_COMPONENT PhysicsComponent,
#define IKIGAI_PHYSICS3D_LEGACY_DESCRIPTOR PhysicsComponent::Descriptor,
#else
#define IKIGAI_PHYSICS3D_COMPONENTS
#define IKIGAI_PHYSICS3D_DESCRIPTORS
#define IKIGAI_PHYSICS3D_LEGACY_COMPONENT
#define IKIGAI_PHYSICS3D_LEGACY_DESCRIPTOR
#endif
#if USE_PHYSICS_2D
#include "components/cloth2dComponent.h"
#include "components/forceField2dComponent.h"
#include "components/physics2dComponent.h"

#define IKIGAI_PHYSICS2D_COMPONENTS                                                                                                                  \
	Physics2DComponent, SoftBody2DComponent, Fluid2DComponent, Joint2DComponent, ForceField2DComponent, Cloth2DComponent,
#define IKIGAI_PHYSICS2D_DESCRIPTORS                                                                                                                 \
	Physics2DComponent::Descriptor, SoftBody2DComponent::Descriptor, Fluid2DComponent::Descriptor, Joint2DComponent::Descriptor,                     \
		ForceField2DComponent::Descriptor, Cloth2DComponent::Descriptor,
#else
#define IKIGAI_PHYSICS2D_COMPONENTS
#define IKIGAI_PHYSICS2D_DESCRIPTORS
#endif
#include "../gui/components/spriteComponent.h"
#include "../gui/components/vectorComponents.h"
#include "components/batchComponent.h"

#ifdef IKIGAI_TEST_MODULE
#include "character/characterDiagnosticsComponent.h"
#if USE_PHYSICS_3D
#define IKIGAI_TEST_COMPONENTS CharacterDiagnosticsComponent,
#define IKIGAI_TEST_DESCRIPTORS CharacterDiagnosticsComponent::Descriptor,
#endif
#endif
#ifndef IKIGAI_TEST_COMPONENTS
#define IKIGAI_TEST_COMPONENTS
#define IKIGAI_TEST_DESCRIPTORS
#endif

namespace IKIGAI::ECS {

#define COMPONENTS_DEF                                                                                                                               \
	IKIGAI_TEST_COMPONENTS BehaviorTreeComponent, BlackboardComponent, VariableContextComponent, SkeletalStateGraphComponent, TransformComponent,    \
		IKIGAI_PHYSICS3D_COMPONENTS IKIGAI_PHYSICS2D_COMPONENTS AmbientLight, AmbientSphereLight, AudioComponent, CameraComponent, DirectionalLight, \
		InputComponent, LogicComponent, MaterialRenderer, ModelRenderer, ModelLODRenderer, RenderTargetComponent, PointLight,                        \
		IKIGAI_LUA_COMPONENT IKIGAI_SCRIPT_COMPONENT SkeletalComponent, SkeletalAnimationComponent, FacialAnimationComponent, SpotLight,             \
		IKIGAI_PHYSICS3D_LEGACY_COMPONENT AudioListenerComponent, VrCameraComponent, BatchComponent, RootGuiComponent, SpriteComponent,              \
		SpriteAnimateComponent, SpriteParticleComponent, ParticleSystem2DComponent, ParticleSystem3DComponent, SpineComponent, LabelComponent,       \
		InteractionComponent, ClipComponent, ScrollComponent, LayoutComponent, ChunkModelRenderer, SdfTextComponent, VectorShapeComponent,           \
		VectorPathComponent, VectorCanvasComponent, EnvironmentComponent, VegetationInteractorComponent, SurfaceComponent, FoliageComponent

#define COMPONENTS_DESCRIPTOR_DEF                                                                                                                    \
	IKIGAI_TEST_DESCRIPTORS BehaviorTreeComponent::Descriptor, BlackboardComponent::Descriptor, VariableContextComponent::Descriptor,                \
		SkeletalStateGraphComponent::Descriptor, TransformComponent::Descriptor,                                                                     \
		IKIGAI_PHYSICS3D_DESCRIPTORS IKIGAI_PHYSICS2D_DESCRIPTORS AmbientLight::Descriptor, AmbientSphereLight::Descriptor,                          \
		AudioComponent::Descriptor, CameraComponent::Descriptor, DirectionalLight::Descriptor, InputComponent::Descriptor,                           \
		LogicComponent::Descriptor, MaterialRenderer::Descriptor, ModelRenderer::Descriptor, ModelLODRenderer::Descriptor,                           \
		RenderTargetComponent::Descriptor, PointLight::Descriptor, IKIGAI_LUA_DESCRIPTOR IKIGAI_SCRIPT_DESCRIPTOR SkeletalComponent::Descriptor,     \
		SkeletalAnimationComponent::Descriptor, FacialAnimationComponent::Descriptor, SpotLight::Descriptor,                                         \
		IKIGAI_PHYSICS3D_LEGACY_DESCRIPTOR AudioListenerComponent::Descriptor, VrCameraComponent::Descriptor, BatchComponent::Descriptor,            \
		RootGuiComponent::Descriptor, SpriteComponent::Descriptor, SpriteAnimateComponent::Descriptor, SpriteParticleComponent::Descriptor,          \
		ParticleSystem2DComponent::Descriptor, ParticleSystem3DComponent::Descriptor, SpineComponent::Descriptor, LabelComponent::Descriptor,        \
		InteractionComponent::Descriptor, ClipComponent::Descriptor, ScrollComponent::Descriptor, LayoutComponent::Descriptor,                       \
		ChunkModelRenderer::Descriptor, SdfTextComponent::Descriptor, VectorShapeComponent::Descriptor, VectorPathComponent::Descriptor,             \
		VectorCanvasComponent::Descriptor, EnvironmentComponent::Descriptor, VegetationInteractorComponent::Descriptor,                              \
		SurfaceComponent::Descriptor, FoliageComponent::Descriptor

//ArCameraComponent,\

template <typename...> class ComponentsTypeProvider { };
using ComponentsTypeProviderType = ComponentsTypeProvider<COMPONENTS_DEF>;

using ComponentsType = std::variant<COMPONENTS_DEF>;
using ComponentsDescriptorType = std::variant<COMPONENTS_DESCRIPTOR_DEF>;
}

namespace serde {
// Components carry a discriminator key (e.g. CameraComponentType). The generic
// serdepp variant reader probes each alternative by deserializing it and catching
// exceptions. Select the descriptor first so valid scenes never take that path.
template <> struct serde_adaptor<nlohmann::json, IKIGAI::ECS::ComponentsDescriptorType> {
	using Variant = IKIGAI::ECS::ComponentsDescriptorType;
	using Reader = void (*)(nlohmann::json&, Variant&);

	template <class... Components> static auto readers(IKIGAI::ECS::ComponentsTypeProvider<Components...>) {
		return std::unordered_map<std::string, Reader> { { IKIGAI::ECS::GetComponentName<Components>() + "Type",
			+[](nlohmann::json& json, Variant& value) {
				if constexpr (std::is_same_v<Components, IKIGAI::ECS::SurfaceComponent> || std::is_same_v<Components, IKIGAI::ECS::FoliageComponent>
					|| std::is_same_v<Components, IKIGAI::ECS::ChunkModelRenderer>) {
					value = IKIGAI::DATA::readDescriptor<typename Components::Descriptor>(json);
				}
				else {
					value = deserialize<typename Components::Descriptor>(json);
				}
			} }... };
	}

	static void from(nlohmann::json& json, std::string_view key, Variant& value) {
		auto& component = key.empty() ? json : json.at(std::string(key));
		if (!component.is_object()) {
			throw std::runtime_error("Component descriptor must be an object");
		}
		static const auto registry = readers(IKIGAI::ECS::ComponentsTypeProviderType {});
		Reader reader = nullptr;
		for (const auto& item : component.items()) {
			const auto found = registry.find(item.key());
			if (found == registry.end()) {
				continue;
			}
			if (reader) {
				throw std::runtime_error("Component descriptor has multiple type keys");
			}
			reader = found->second;
		}
		if (!reader) {
			throw std::runtime_error("Component descriptor has no supported type key");
		}
		// A malformed selected descriptor is an error, not another variant to try.
		reader(component, value);
	}

	static void into(nlohmann::json& json, std::string_view key, const Variant& value) {
		std::visit([&](const auto& descriptor) { serialize_to<nlohmann::json>(descriptor, json, key); }, value);
	}
};
}
