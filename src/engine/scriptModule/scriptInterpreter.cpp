#if USE_LUA
#include "scriptInterpreter.h"

#include <algorithm>
#include <sol/sol.hpp>
#include <string>

#include "luaBinder.h"
#include "scriptFileWatcher.h"
#include "scriptVfsPath.h"

#include <coreModule/ecs/components/scriptComponent.h>
#include <coreModule/ecs/object.h>
#include <coreModule/ecs/systems/scriptSystem.h>

#include "ecsModule/world.h"
#include "utilsModule/log/loggerDefine.h"

using namespace IKIGAI;
using namespace IKIGAI::SCRIPTING;

ECS::ScriptComponent* ScriptInterpreter::TrackedScript::resolve() const {
	const auto manager = components.lock();
	return manager ? manager->resolveComponent(handle) : nullptr;
}

ScriptInterpreter::TrackedScript ScriptInterpreter::capture(ECS::ScriptComponent& component) {
	auto& world = component.obj->getWorld();
	TrackedScript tracked { world.getComponentManager()->getComponentHandle<ECS::ScriptComponent>(component.obj->getID()),
		world.getComponentManagerWeak() };
	return tracked.resolve() == &component ? tracked : TrackedScript {};
}

ScriptInterpreter::TrackedScript* ScriptInterpreter::findScript(const ECS::ComponentHandle<ECS::ScriptComponent>& handle) {
	const auto it = std::ranges::find(scripts, handle, &TrackedScript::handle);
	return it != scripts.end() ? &*it : nullptr;
}

void ScriptInterpreter::pruneScripts() {
	for (auto script = scripts.begin(); script != scripts.end();) {
		if (script->resolve()) {
			++script;
		}
		else {
			detachFileWatch(*script);
			script = scripts.erase(script);
		}
	}
}

ScriptInterpreter::ScriptInterpreter(const std::string& scriptRootFolder)
	: scriptRootFolder(scriptRootFolder) {
	createLuaContextAndBindGlobals();

	mCreatedListener = ECS::ScriptComponentEvents::createdEvent.add(std::bind(&ScriptInterpreter::consider, this, std::placeholders::_1));
	try {
		mDestroyedListener = ECS::ScriptComponentEvents::destroyedEvent.add(std::bind(&ScriptInterpreter::unconsider, this, std::placeholders::_1));
	} catch (...) {
		ECS::ScriptComponentEvents::createdEvent.removeListener(mCreatedListener);
		throw;
	}
}

ScriptInterpreter::~ScriptInterpreter() {
	ECS::ScriptComponentEvents::createdEvent.removeListener(mCreatedListener);
	ECS::ScriptComponentEvents::destroyedEvent.removeListener(mDestroyedListener);
	destroyLuaContext();
}

void ScriptInterpreter::createLuaContextAndBindGlobals() {
	if (!luaState) {
		luaState = std::make_unique<sol::state>();
		luaState->open_libraries(sol::lib::base, sol::lib::math);
		LuaBinder::CallBinders(*luaState);
		sharedBindings = luaState->globals();
		checkOk = true;

		pruneScripts();
		for (auto& tracked : scripts) {
			if (auto* script = tracked.resolve(); script && !registerScript(tracked, *script)) {
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
		for (auto& tracked : scripts) {
			detachFileWatch(tracked);
			if (auto* script = tracked.resolve()) {
				unregisterScript(*script);
			}
		}

		// Registry references must be released while their Lua state is still alive.
		sharedBindings = sol::lua_nil;
		luaState.reset();
		checkOk = false;
	}
}

bool ScriptInterpreter::registerScript(TrackedScript& tracked, ECS::ScriptComponent& component) {
	auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
	auto systemManager = world.getSystemManager();
	const bool ok = systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->registerToLuaContext(component, *luaState, sharedBindings);
	attachFileWatch(tracked, component);
	return ok;
}

void ScriptInterpreter::unregisterScript(IKIGAI::ECS::ScriptComponent& component) {
	auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
	auto systemManager = world.getSystemManager();
	systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->unregisterFromLuaContext(component);
}

void ScriptInterpreter::attachFileWatch(TrackedScript& tracked, const ECS::ScriptComponent& component) {
#if defined(USE_FILE_WATCHER)
	if (tracked.fileWatchHandle != 0) {
		return;
	}
	const auto vfsPath = LuaScriptVfsPath(component.getScriptName());
	tracked.fileWatchHandle = ScriptFileWatcher::watch(vfsPath, [this, handle = tracked.handle]() {
		if (auto* tracked = findScript(handle); luaState && tracked) {
			if (auto* script = tracked->resolve()) {
				reloadScript(*tracked, *script);
			}
		}
	});
#else
	(void)component;
	(void)tracked;
#endif
}

void ScriptInterpreter::detachFileWatch(TrackedScript& tracked) {
#if defined(USE_FILE_WATCHER)
	if (tracked.fileWatchHandle != 0) {
		ScriptFileWatcher::unwatch(tracked.fileWatchHandle);
		tracked.fileWatchHandle = 0;
	}
#else
	(void)tracked;
#endif
}

void ScriptInterpreter::consider(UTILS::WeakPtr<ECS::ScriptComponent> script) {
	if (!luaState || !script || script->getScriptName().empty()) {
		return;
	}
	const auto identity = capture(*script);
	if (!identity.handle) {
		return;
	}
	pruneScripts();
	if (auto* tracked = findScript(identity.handle)) {
		reloadScript(*tracked, *script);
		return;
	}
	if (!registerScript(scripts.emplace_back(identity), *script)) {
		checkOk = false;
	}
}

void ScriptInterpreter::unconsider(UTILS::WeakPtr<ECS::ScriptComponent> script) {
	if (!script) {
		pruneScripts();
		return;
	}
	const auto identity = capture(*script);
	const auto it = std::ranges::find(scripts, identity.handle, &TrackedScript::handle);
	if (it != scripts.end()) {
		detachFileWatch(*it);
		scripts.erase(it);
	}
	if (luaState) {
		unregisterScript(*script);
	}
}

bool ScriptInterpreter::reloadScript(const UTILS::WeakPtr<ECS::ScriptComponent>& script) {
	if (!luaState || !script) {
		return false;
	}
	const auto identity = capture(*script);
	if (!identity.handle || script->getScriptName().empty()) {
		return false;
	}
	auto* tracked = findScript(identity.handle);
	return reloadScript(tracked ? *tracked : scripts.emplace_back(identity), *script);
}

bool ScriptInterpreter::reloadScript(TrackedScript& tracked, ECS::ScriptComponent& script) {
	if (script.getScriptName().empty()) {
		return false;
	}
	detachFileWatch(tracked);
	unregisterScript(script);
	const bool ok = registerScript(tracked, script);
	if (ok) {
		auto& world = RESOURCES::ServiceManager::Get<ECS::World>();
		auto systemManager = world.getSystemManager();
		systemManager->getSystem<ECS::ScriptSystem>("ScriptSystem")->invokeOnReload(script);
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
	pruneScripts();
	for (auto& tracked : scripts) {
		if (auto* script = tracked.resolve(); script && LuaScriptVfsPath(script->getScriptName()) == vfsPath) {
			any = true;
			allOk = reloadScript(tracked, *script) && allOk;
		}
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
