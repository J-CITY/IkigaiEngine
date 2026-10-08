#include "luaComponentBinder.h"

#include <sol/sol.hpp>

#include <coreModule/ecs/object.h>
#include <coreModule/ecs/components/transform.h>
#include <coreModule/ecs/components/cameraComponent.h>
#include <coreModule/ecs/components/directionalLight.h>
#include <coreModule/ecs/components/pointLight.h>
#include <coreModule/ecs/components/spotLight.h>
#include <coreModule/ecs/components/ambientLight.h>
#include <coreModule/ecs/components/ambientSphereLight.h>
#include <coreModule/ecs/components/modelRenderer.h>
#include <coreModule/ecs/components/materialRenderer.h>

using namespace IKIGAI::SCRIPTING;

void LuaComponentBinder::BindComponent(sol::state & p_luaState)
{
	using namespace IKIGAI::MATH;
	using namespace IKIGAI::ECS;
	
	p_luaState.new_usertype<ComponentBase>("Component",
		"GetOwner", [](ComponentBase& p_component) -> const Object& { return p_component.obj; }
	);

	p_luaState.new_usertype<TransformComponent>("Transform",
		sol::base_classes, sol::bases<ComponentBase>(),
		/* Methods */
		"SetPosition", &TransformComponent::setLocalPosition,
		"SetRotation", &TransformComponent::setLocalRotation,
		"SetScale", &TransformComponent::setLocalScale,
		"SetLocalPosition", &TransformComponent::setLocalPosition,
		"SetLocalRotation", &TransformComponent::setLocalRotation,
		"SetLocalScale", &TransformComponent::setLocalScale,
		"GetPosition", [](TransformComponent& p_this) -> Vector3f { return p_this.getWorldPosition(); },
		"GetRotation", [](TransformComponent& p_this) -> QuaternionF { return p_this.getWorldRotation(); },
		"GetScale", [](TransformComponent& p_this) -> Vector3f { return p_this.getWorldScale(); },
		"GetLocalPosition", [](TransformComponent& p_this) -> Vector3f { return p_this.getLocalPosition(); },
		"GetLocalRotation", [](TransformComponent& p_this) -> QuaternionF { return p_this.getLocalRotation(); },
		"GetLocalScale", [](TransformComponent& p_this) -> Vector3f { return p_this.getLocalScale(); },
		"GetWorldPosition", [](TransformComponent& p_this) -> Vector3f { return p_this.getWorldPosition(); },
		"GetWorldRotation", [](TransformComponent& p_this) -> QuaternionF { return p_this.getWorldRotation(); },
		"GetWorldScale", [](TransformComponent& p_this) -> Vector3f { return p_this.getWorldScale(); },
		"GetForward", &TransformComponent::getWorldForward,
		"GetUp", &TransformComponent::getWorldUp,
		"GetRight", &TransformComponent::getWorldRight,
		"GetLocalForward", &TransformComponent::getLocalForward,
		"GetLocalUp", &TransformComponent::getLocalUp,
		"GetLocalRight", &TransformComponent::getLocalRight,
		"GetWorldForward", &TransformComponent::getWorldForward,
		"GetWorldUp", &TransformComponent::getWorldUp,
		"GetWorldRight", &TransformComponent::getWorldRight
		);
    
    p_luaState.new_enum<IKIGAI::ECS::EFrustumBehaviour>("FrustumBehaviour",
        {
            {"DISABLED",		IKIGAI::ECS::EFrustumBehaviour::DISABLED},
            {"CULL_MODEL",		IKIGAI::ECS::EFrustumBehaviour::CULL_MODEL},
            {"CULL_MESHES",		IKIGAI::ECS::EFrustumBehaviour::CULL_MESHES},
            {"CULL_CUSTOM",		IKIGAI::ECS::EFrustumBehaviour::CULL_CUSTOM}
        });

	p_luaState.new_usertype<ModelRenderer>("ModelRenderer",
		sol::base_classes, sol::bases<ComponentBase>(),
		"GetModel", &ModelRenderer::getModel,
		"SetModel", &ModelRenderer::setModel,
		"GetFrustumBehaviour", &ModelRenderer::getFrustumBehaviour,
		"SetFrustumBehaviour", &ModelRenderer::setFrustumBehaviour
	);

	p_luaState.new_usertype<MaterialRenderer>("MaterialRenderer",
		sol::base_classes, sol::bases<ComponentBase>(),
		"SetMaterial", &MaterialRenderer::setMaterial
	);

    p_luaState.new_enum<IKIGAI::RENDER::Camera::ProjectionMode>("ProjectionMode",
    {
        {"ORTHOGRAPHIC",	IKIGAI::RENDER::Camera::ProjectionMode::ORTHOGRAPHIC},
        {"PERSPECTIVE",		IKIGAI::RENDER::Camera::ProjectionMode::PERSPECTIVE}
    });

	p_luaState.new_usertype<CameraComponent>("Camera",
		sol::base_classes, sol::bases<ComponentBase>(),
		"GetFov", &CameraComponent::getFov,
		"GetSize", &CameraComponent::getSize,
		"GetNear", &CameraComponent::getNear,
		"GetFar", &CameraComponent::getFar,
		"SetFov", &CameraComponent::setFov,
		"SetSize", &CameraComponent::setSize,
		"SetNear", &CameraComponent::setNear,
		"SetFar", &CameraComponent::setFar,
        "HasFrustumGeometryCulling", &CameraComponent::isFrustumGeometryCulling,
        "HasFrustumLightCulling", &CameraComponent::isFrustumLightCulling,
        "GetProjectionMode", &CameraComponent::getProjectionMode,
        "SetFrustumGeometryCulling", &CameraComponent::setFrustumGeometryCulling,
        "SetFrustumLightCulling", &CameraComponent::setFrustumLightCulling,
        "SetProjectionMode", &CameraComponent::setProjectionMode
		);
	/*
	p_luaState.new_usertype<LightComponent>("Light",
		sol::base_classes, sol::bases<LightComponent>(),
		"GetColor", &PointLight::getColor,
		"GetIntensity", &PointLight::getIntensity,
		"SetColor", &PointLight::setColor,
		"SetIntensity", &PointLight::setIntensity
		);*/

	p_luaState.new_usertype<PointLight>("PointLight",
		sol::base_classes, sol::bases<LightComponent>(),
		"GetConstant", &PointLight::getConstant,
		"GetLinear", &PointLight::getLinear,
		"GetQuadratic", &PointLight::getQuadratic,
		"SetConstant", &PointLight::setConstant,
		"SetLinear", &PointLight::setLinear,
		"SetQuadratic", &PointLight::setQuadratic
		);

	p_luaState.new_usertype<SpotLight>("SpotLight",
		sol::base_classes, sol::bases<LightComponent>(),
		"GetConstant", &SpotLight::getConstant,
		"GetLinear", &SpotLight::getLinear,
		"GetQuadratic", &SpotLight::getQuadratic,
		"GetCutOff", &SpotLight::getCutoff,
		"GetOuterCutOff", &SpotLight::getOuterCutoff,
		"SetConstant", &SpotLight::setConstant,
		"SetLinear", &SpotLight::setLinear,
		"SetQuadratic", &SpotLight::setQuadratic,
		"SetCutOff", &SpotLight::setCutoff,
		"SetOuterCutOff", &SpotLight::setOuterCutoff
		);

	p_luaState.new_usertype<AmbientLight>("AmbientBoxLight",
		sol::base_classes, sol::bases<LightComponent>(),
		"GetSize", &AmbientLight::getSize,
		"SetSize", &AmbientLight::setSize
		);

	p_luaState.new_usertype<AmbientSphereLight>("AmbientSphereLight",
		sol::base_classes, sol::bases<LightComponent>(),
		"GetRadius", &AmbientSphereLight::getRadius,
		"SetRadius", &AmbientSphereLight::setRadius
		);

	p_luaState.new_usertype<DirectionalLight>("DirectionalLight",
		sol::base_classes, sol::bases<LightComponent>()
		);
}
