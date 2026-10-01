#include "scriptInterpreter.h"

#include <string>
#include <sol/sol.hpp>

#include "luaBinder.h"
#include <coreModule/ecs/components/scriptComponent.h>
#include <coreModule/ecs/componentManager.h>
#include <coreModule/ecs/systems/scriptSystem.h>

#include "ecsModule/world.h"
#include "utilsModule/log/loggerDefine.h"


using namespace IKIGAI;
using namespace IKIGAI::SCRIPTING;

ScriptInterpreter::ScriptInterpreter(const std::string& scriptRootFolder) :
	scriptRootFolder(scriptRootFolder) {
	createLuaContextAndBindGlobals();

	IKIGAI::ECS::ScriptComponentEvents::createdEvent.add(std::bind(&ScriptInterpreter::consider, this, std::placeholders::_1));
	IKIGAI::ECS::ScriptComponentEvents::destroyedEvent.add(std::bind(&ScriptInterpreter::unconsider, this, std::placeholders::_1));
}

ScriptInterpreter::~ScriptInterpreter() {
	destroyLuaContext();
}

void ScriptInterpreter::createLuaContextAndBindGlobals() {
	if (!luaState) {
		luaState = std::make_unique<sol::state>();
		luaState->open_libraries(sol::lib::base, sol::lib::math);
		LuaBinder::CallBinders(*luaState);
		checkOk = true;
		
		for (const auto& s : scripts) {
			auto& world = RESOURCES::ServiceManager::Get<ECS2::World>();
			auto systemManager = world.getSystemManager();
			if (!systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->registerToLuaContext(*s, *luaState, scriptRootFolder)) {
				//if (!ECS::ComponentManager::GetInstance().getSystemManager().getSystem<ECS::ScriptSystem>()->registerToLuaContext(*s, *luaState, scriptRootFolder)) {
				checkOk = false;
			}
		}

		if (!checkOk) {
			LOG_ERROR << ("ScriptComponent interpreter failed to register scripts. Check your lua scripts");
		}
	}
}

void ScriptInterpreter::destroyLuaContext() {
	if (luaState) {
		for (const auto& s : scripts) {
			auto& world = RESOURCES::ServiceManager::Get<ECS2::World>();
			auto systemManager = world.getSystemManager();
			systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->unregisterFromLuaContext(*s);
			//ECS::ComponentManager::GetInstance().getSystemManager().getSystem<ECS::ScriptSystem>()->unregisterFromLuaContext(*s);
		}

		luaState.reset();
		checkOk = false;
	}
}

void ScriptInterpreter::consider(UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> s) {
	if (s->getScriptName().empty()) {
		return;
	}
	if (luaState) {
		scripts.push_back(s);

		auto& world = RESOURCES::ServiceManager::Get<ECS2::World>();
		auto systemManager = world.getSystemManager();
		if (!systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->registerToLuaContext(*s, *luaState, scriptRootFolder)) {
		//if (!ECS::ComponentManager::GetInstance().getSystemManager().getSystem<ECS::ScriptSystem>()->registerToLuaContext(*s, *luaState, scriptRootFolder)) {
			checkOk = false;
		}
	}
}

void ScriptInterpreter::unconsider(UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> p_toUnconsider) {
	if (luaState) {
		//ECS::ComponentManager::GetInstance().getSystemManager().getSystem<ECS::ScriptSystem>()->unregisterFromLuaContext(*p_toUnconsider);
		auto& world = RESOURCES::ServiceManager::Get<ECS2::World>();
		auto systemManager = world.getSystemManager();
		systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->unregisterFromLuaContext(*p_toUnconsider);
	}
	scripts.erase(std::remove_if(scripts.begin(), scripts.end(), [p_toUnconsider](const UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> s) {
		return p_toUnconsider == s;
	}), scripts.end());

	refreshAll();
}

void ScriptInterpreter::refreshAll() {
	destroyLuaContext();
	createLuaContextAndBindGlobals();
}

bool ScriptInterpreter::isOk() const {
	return checkOk;
}
