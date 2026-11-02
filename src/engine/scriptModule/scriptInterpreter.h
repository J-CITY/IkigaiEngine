#if USE_LUA
#pragma once

#include "utilsModule/weakPtr.h"
#include <coreModule/ecs/components/scriptComponent.h>
#include <memory>
#include <sol/sol.hpp>
#include <string>
#include <vector>

namespace IKIGAI::SCRIPTING {
class ScriptInterpreter {
public:
	explicit ScriptInterpreter(const std::string& scriptRootFolder);
	~ScriptInterpreter();

	void createLuaContextAndBindGlobals();
	void destroyLuaContext();
	void consider(UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> toConsider);
	void unconsider(UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent> toUnconsider);
	void refreshAll();
	bool reloadScript(const UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent>& script);
	bool reloadScriptsAtVfsPath(const std::string& vfsPath);
	[[nodiscard]] bool isOk() const;

private:
	bool registerScript(IKIGAI::ECS::ScriptComponent& component);
	void unregisterScript(IKIGAI::ECS::ScriptComponent& component);
	void attachFileWatch(IKIGAI::ECS::ScriptComponent& component);
	void detachFileWatch(IKIGAI::ECS::ScriptComponent& component);

	std::unique_ptr<sol::state> luaState;
	sol::table sharedBindings {};
	std::string scriptRootFolder;
	std::vector<UTILS::WeakPtr<IKIGAI::ECS::ScriptComponent>> scripts;
	bool checkOk = false;
};
}

#endif
