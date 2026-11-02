#if USE_LUA
#include "scriptSystem.h"
#include <algorithm>
#include <resourceModule/fileSystem/fileSystem.h>
#include <resourceModule/serviceManager.h>
#include <sceneModule/sceneManager.h>
#include <scriptModule/scriptSource.h>
#include <scriptModule/scriptVfsPath.h>
#ifdef __EMSCRIPTEN__
#include <iostream>
#endif

IKIGAI::ECS::ScriptSystem::ScriptSystem() {
	mName = "ScriptSystem";
	mPriority = 0;
	mExclusiveBatch = true;
	auto cm = RESOURCES::ServiceManager::Get<ECS::World>().getComponentManager();
	mReads.insert(cm->getComponentType<ScriptComponent>());
};

void IKIGAI::ECS::ScriptSystem::onAwake(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnAwake");
	}
}

void IKIGAI::ECS::ScriptSystem::onStart(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnStart");
	}
}

void IKIGAI::ECS::ScriptSystem::onEnable(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnEnable");
	}
}

void IKIGAI::ECS::ScriptSystem::onDisable(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnDisable");
	}
}

void IKIGAI::ECS::ScriptSystem::onDestroy(ECS::World& world) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnDestroy");
	}
}

void IKIGAI::ECS::ScriptSystem::onUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnUpdate", static_cast<float>(dt.count()));
	}
}

void IKIGAI::ECS::ScriptSystem::onFixedUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnFixedUpdate", static_cast<float>(dt.count()));
	}
}

void IKIGAI::ECS::ScriptSystem::onLateUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) {
	for (auto& component : world.getComponentManager()->getComponentsArray<ECS::ScriptComponent>()) {
		luaCall(component, "OnLateUpdate", static_cast<float>(dt.count()));
	}
}

bool IKIGAI::ECS::ScriptSystem::registerToLuaContext(ScriptComponent& component, sol::state& p_luaState, const sol::table& sharedBindings) {
	using namespace IKIGAI::SCRIPTING;

	auto& object = component.getTable();
	const auto& scriptName = component.getScriptName();
	const auto vfsPath = LuaScriptVfsPath(scriptName);
#ifdef __EMSCRIPTEN__
	std::cout << "[web] lua load '" << scriptName << "' vfs=" << vfsPath << std::endl;
	std::cout.flush();
#endif
	auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(vfsPath);
	if (!file || !file->isValid()) {
		LOG_ERROR << "Lua script '" << vfsPath << "': file not found in VFS";
#ifdef __EMSCRIPTEN__
		std::cout << "[web] lua load FAILED " << vfsPath << " (missing in VFS)" << std::endl;
		std::cout.flush();
#endif
		return false;
	}

	component.scriptEnvironment = sol::environment(p_luaState, sol::create, sharedBindings);

	const auto source = NormalizeScriptSource(file->readStr());
	const auto chunkName = "@" + vfsPath;
	auto result = p_luaState.safe_script(source, component.scriptEnvironment, sol::script_pass_on_error, chunkName);
	if (!result.valid()) {
		sol::error err = result;
		LOG_ERROR << "Lua script '" << vfsPath << "': " << err.what();
#ifdef __EMSCRIPTEN__
		std::cout << "[web] lua load FAILED " << vfsPath << " " << err.what() << std::endl;
		std::cout.flush();
#endif
		return false;
	}
	if (result.return_count() == 1 && result[0].is<sol::table>()) {
		object = result[0];
		object["owner"] = component.obj.getPtr();
		component.loadedVfsPath = vfsPath;
#ifdef __EMSCRIPTEN__
		std::cout << "[web] lua load ok " << scriptName << std::endl;
		std::cout.flush();
#endif
		return true;
	}
	LOG_ERROR << ("'" + scriptName + "' missing return expression");
#ifdef __EMSCRIPTEN__
	std::cout << "[web] lua missing return " << scriptName << std::endl;
	std::cout.flush();
#endif
	return false;
}

void IKIGAI::ECS::ScriptSystem::unregisterFromLuaContext(ScriptComponent& component) {
	component.getTable() = sol::lua_nil;
	component.scriptEnvironment = sol::environment();
	component.loadedVfsPath.clear();
}

void IKIGAI::ECS::ScriptSystem::invokeOnReload(ScriptComponent& component) {
	auto& object = component.getTable();
	if (!object.valid()) {
		return;
	}
	sol::object reloadCallback = object["onReload"];
	if (!reloadCallback.valid()) {
		reloadCallback = object["OnReload"];
	}
	if (!reloadCallback.valid() || reloadCallback.get_type() != sol::type::function) {
		return;
	}
	sol::protected_function reloadFn = reloadCallback;
	const auto reloadResult = reloadFn.call(object);
	if (!reloadResult.valid()) {
		sol::error err = reloadResult;
		LOG_ERROR << "Lua onReload '" << component.getScriptName() << "': " << err.what();
	}
}

#endif
