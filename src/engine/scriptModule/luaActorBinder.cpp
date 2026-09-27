#include "luaActorBinder.h"

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
#include <coreModule/ecs/components/scriptComponent.h>

using namespace IKIGAI::SCRIPTING;

void IKIGAI::SCRIPTING::LuaActorBinder::BindActor(sol::state & p_luaState) {
	using namespace IKIGAI::ECS;

	p_luaState.new_usertype<Object>("Object",
		/* Methods */
		"GetName", &Object::getName,
		"SetName", &Object::setName,
		"GetTag", &Object::getTag,
		"GetChildren", &Object::getChildren,
		"SetTag", &Object::setTag,
		"GetID", &Object::getID,
		"GetParent", &Object::getParent,
		"SetParent", &Object::setParent,
		"DetachFromParent", &Object::detachFromParent,
		"Destroy", &Object::markAsDestroy,
		"IsSelfActive", &Object::getIsSelfActive,
		"IsActive", &Object::getIsActive,
		"SetActive", &Object::setActive,


		/* Components Getters */
		"GetTransform", [](Object& p_this) -> TransformComponent* {
			return p_this.getComponent<TransformComponent>().get();
		},
		"GetCamera", [](Object& p_this) -> CameraComponent* {
			return p_this.getComponent<CameraComponent>().get();
		},
		//"GetLight", [](Object& p_this) -> LightComponent* {
		//	return p_this.getComponent<LightComponent>().get();
		//},
		"GetPointLight", [](Object& p_this) -> PointLight* {
			return p_this.getComponent<PointLight>().get();
		},
		"GetSpotLight", [](Object& p_this) -> SpotLight* {
			return p_this.getComponent<SpotLight>().get();
		},
		"GetDirectionalLight", [](Object& p_this) -> DirectionalLight* {
			return p_this.getComponent<DirectionalLight>().get();
		},
		"GetAmbientBoxLight", [](Object& p_this) -> AmbientLight* {
			return p_this.getComponent<AmbientLight>().get();
		},
		"GetAmbientSphereLight", [](Object& p_this) -> AmbientSphereLight* {
			return p_this.getComponent<AmbientSphereLight>().get();
		},
		"GetModelRenderer", [](Object& p_this) -> ModelRenderer* {
			return p_this.getComponent<ModelRenderer>().get();
		},
		"GetMaterialRenderer", [](Object& p_this) -> MaterialRenderer* {
			return p_this.getComponent<MaterialRenderer>().get();
		},
		/* Behaviours relatives */
		//"GetBehaviour", [](Object& p_this, const std::string& p_name) -> sol::table {
		//	auto behaviour = p_this.getScript(p_name);
		//	if (behaviour)
		//		return behaviour->getTable();
		//	else
		//		return sol::lua_nil;
		//},
		
		//TODO:
		/* Components Creators */
		"AddTransform", [](Object& p_this) -> TransformComponent* {
			return p_this.addComponent<TransformComponent>().get();
		},
		//"AddModelRenderer", &Object::addComponent<ModelRenderer>,
		//"AddCamera", &Object::addComponent<CameraComponent>,
		//"AddPointLight", &Object::addComponent<PointLight>,
		//"AddSpotLight", &Object::addComponent<SpotLight>,
		//"AddDirectionalLight", &Object::addComponent<DirectionalLight>,
		//"AddAmbientBoxLight", &Object::addComponent<AmbientLight>,
		//"AddAmbientSphereLight", &Object::addComponent<AmbientSphereLight>,
		//"AddMaterialRenderer", &Object::addComponent<MaterialRenderer>,
		
		/* Components Destructors */
		"RemoveModelRenderer", &Object::removeComponent<ModelRenderer>,
		"RemoveCamera", &Object::removeComponent<CameraComponent>,
		"RemovePointLight", &Object::removeComponent<PointLight>,
		"RemoveSpotLight", &Object::removeComponent<SpotLight>,
		"RemoveDirectionalLight", &Object::removeComponent<DirectionalLight>,
		"RemoveAmbientBoxLight", &Object::removeComponent<AmbientLight>,
		"RemoveAmbientSphereLight", &Object::removeComponent<AmbientSphereLight>,
		"RemoveMaterialRenderer", &Object::removeComponent<MaterialRenderer>//,
		
		/* Behaviour management */
		//"AddBehaviour", &Object::addScript,
		//"RemoveBehaviour", sol::overload
		//(
		//	//sol::resolve<bool(ScriptComponent&)>(&Object::removeScript),
		//	sol::resolve<bool(const std::string&)>(&Object::removeScript)
		//)
	);
}
