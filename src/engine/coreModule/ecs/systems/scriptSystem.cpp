#include "scriptSystem.h"
#include <sceneModule/sceneManager.h>

IKIGAI::ECS::ScriptSystem::ScriptSystem() {
	mName = "ScriptSystem";
	auto cm = RESOURCES::ServiceManager::Get<ECS2::World>().getComponentManager();
	mReads.insert(cm->getComponentType<ScriptComponent>());
};

void IKIGAI::ECS::ScriptSystem::onAwake(ECS2::World& world) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnAwake");
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnAwake");
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnAwake");
	//}
}

void IKIGAI::ECS::ScriptSystem::onStart(ECS2::World& world) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnStart");
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnStart");
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnStart");
	//}
}

void IKIGAI::ECS::ScriptSystem::onEnable(ECS2::World& world) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnEnable");
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnEnable");
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnEnable");
	//}
}

void IKIGAI::ECS::ScriptSystem::onDisable(ECS2::World& world) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnDisable");
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnDisable");
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnDisable");
	//}
}

void IKIGAI::ECS::ScriptSystem::onDestroy(ECS2::World& world) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnDestroy");
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnDestroy");
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnDestroy");
	//}
}

void IKIGAI::ECS::ScriptSystem::onUpdate(ECS2::World& world, ECS2::CommandBuffer& cb, std::chrono::duration<double> dt) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnUpdate", static_cast<float>(dt.count()));
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnUpdate", static_cast<float>(dt.count()));
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnUpdate", static_cast<float>(dt.count()));
	//}
}

void IKIGAI::ECS::ScriptSystem::onFixedUpdate(ECS2::World& world, ECS2::CommandBuffer& cb, std::chrono::duration<double> dt) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnFixedUpdate", static_cast<float>(dt.count()));
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnFixedUpdate", static_cast<float>(dt.count()));
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnFixedUpdate", static_cast<float>(dt.count()));
	//}
}

void IKIGAI::ECS::ScriptSystem::onLateUpdate(ECS2::World& world, ECS2::CommandBuffer& cb, std::chrono::duration<double> dt) {
	//world.getComponentManager()->forEach<ECS::ScriptComponent>(
	//	[&](IKIGAI::ECS2::Entity e, ECS::ScriptComponent& component) {
	//		luaCall(component, "OnLateUpdate", static_cast<float>(dt.count()));
	//	}
	//);
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnLateUpdate", static_cast<float>(dt.count()));
	}
	//for (auto& component : ECS::ComponentManager::GetInstance().getComponentArrayRef<ECS::ScriptComponent>()) {
	//	luaCall(component, "OnLateUpdate", static_cast<float>(dt.count()));
	//}
}

bool IKIGAI::ECS::ScriptSystem::registerToLuaContext(ScriptComponent& component, sol::state& p_luaState, const std::string& p_scriptFolder) {
	using namespace IKIGAI::SCRIPTING;

	auto& object = component.getTable();
	// getName() is ComponentBase::getName() -> always "ScriptComponent"; the script file name is stored in getScriptName()
	const auto& scriptName = component.getScriptName();
	const auto scriptPath = p_scriptFolder + scriptName + ".lua";
	auto result = p_luaState.safe_script_file(scriptPath, &sol::script_pass_on_error);
	if (!result.valid()) {
		sol::error err = result;
		LOG_ERROR << "Lua script '" << scriptPath << "': " << err.what();
		return false;
	}
	if (result.return_count() == 1 && result[0].is<sol::table>()) {
		object = result[0];
		object["owner"] = component.obj.getPtr();
		return true;
	}
	LOG_ERROR << ("'" + scriptName + ".lua' missing return expression");
	return false;
}

void IKIGAI::ECS::ScriptSystem::unregisterFromLuaContext(ScriptComponent& component) {
	component.getTable() = sol::lua_nil;
}
