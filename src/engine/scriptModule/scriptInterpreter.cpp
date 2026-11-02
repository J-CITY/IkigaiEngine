#if USE_LUA
#include "scriptInterpreter.h"

#include <algorithm>
#include <sol/sol.hpp>
#include <string>

#include "luaBinder.h"
#include "scriptFileWatcher.h"
#include "scriptVfsPath.h"

#include <coreModule/ecs/components/scriptComponent.h>
#include <coreModule/ecs/systems/scriptSystem.h>

#include "ecsModule/world.h"
#include "utilsModule/log/loggerDefine.h"

using namespace IKIGAI;
using namespace IKIGAI::SCRIPTING;

ScriptInterpreter::ScriptInterpreter(const std::string& scriptRootFolder)
	: scriptRootFolder(scriptRootFolder) {
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
		sharedBindings = luaState->globals();
		checkOk = true;

		for (const auto& s : scripts) {
			if (!s) {
				continue;
			}
			if (!registerScript(*s)) {
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
			if (!s) {
				continue;
			}
			detachFileWatch(*s);
			unregisterScript(*s);
		}

		luaState.reset();
		sharedBindings = sol::lua_nil;
		checkOk = false;
	}
}

bool ScriptInterpreter::registerScript(IKIGAI::ECS::ScriptComponent& component) {
	auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
	auto systemManager = world.getSystemManager();
	const bool ok = systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->registerToLuaContext(component, *luaState, sharedBindings);
	attachFileWatch(component);
	return ok;
}

void ScriptInterpreter::unregisterScript(IKIGAI::ECS::ScriptComponent& component) {
	auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
	auto systemManager = world.getSystemManager();
	systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->unregisterFromLuaContext(component);
}

void ScriptInterpreter::attachFileWatch(IKIGAI::ECS::ScriptComponent& component) {
#if defined(USE_FILE_WATCHER)
	if (component.fileWatchHandle != 0) {
		return;
	}
	const auto vfsPath = LuaScriptVfsPath(component.getScriptName());
	const auto weak = component.getWeak<IKIGAI::ECS::ScriptComponent>();
	component.fileWatchHandle = ScriptFileWatcher::watch(vfsPath, [this, weak]() {
		if (weak) {
			reloadScript(weak);
		}
	});
#else
	(void)component;
#endif
}

void ScriptInterpreter::detachFileWatch(IKIGAI::ECS::ScriptComponent& component) {
#if defined(USE_FILE_WATCHER)
	if (component.fileWatchHandle != 0) {
		ScriptFileWatcher::unwatch(component.fileWatchHandle);
		component.fileWatchHandle = 0;
	}
#else
	(void)component;
#endif
}

void ScriptInterpreter::consider(UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> s) {
	if (!s || s->getScriptName().empty()) {
		return;
	}
	const auto alreadyTracked
		= std::find_if(scripts.begin(), scripts.end(), [&](const UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent>& tracked) { return tracked == s; });
	if (alreadyTracked != scripts.end()) {
		reloadScript(s);
		return;
	}

	if (luaState) {
		scripts.push_back(s);
		if (!registerScript(*s)) {
			checkOk = false;
		}
	}
}

void ScriptInterpreter::unconsider(UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> p_toUnconsider) {
	if (luaState && p_toUnconsider) {
		detachFileWatch(*p_toUnconsider);
		unregisterScript(*p_toUnconsider);
	}
	scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
					  [p_toUnconsider](const UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> s) { return p_toUnconsider == s; }),
		scripts.end());
}

bool ScriptInterpreter::reloadScript(const UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent>& script) {
	if (!luaState || !script || script->getScriptName().empty()) {
		return false;
	}

	detachFileWatch(*script);
	unregisterScript(*script);
	const bool ok = registerScript(*script);
	if (ok) {
		auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
		auto systemManager = world.getSystemManager();
		systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->invokeOnReload(*script);
	}
	checkOk = ok;
	return ok;
}

bool ScriptInterpreter::reloadScriptsAtVfsPath(const std::string& vfsPath) {
	if (!luaState) {
		return false;
	}
	bool any = false;
	bool allOk = true;
	for (const auto& script : scripts) {
		if (!script) {
			continue;
		}
		if (LuaScriptVfsPath(script->getScriptName()) != vfsPath) {
			continue;
		}
		any = true;
		allOk = reloadScript(script) && allOk;
	}
	return any && allOk;
}

void ScriptInterpreter::refreshAll() {
	destroyLuaContext();
	createLuaContextAndBindGlobals();
}

bool ScriptInterpreter::isOk() const {
	return checkOk;
}

#endif
