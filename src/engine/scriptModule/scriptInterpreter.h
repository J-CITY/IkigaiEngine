#if USE_LUA
#pragma once

#include "ecsModule/componentHandle.h"
#include "utilsModule/weakPtr.h"
#include <coreModule/ecs/components/scriptComponent.h>
#include <cstdint>
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
	struct TrackedScript {
		ECS::ComponentHandle<ECS::ScriptComponent> handle {};
		std::weak_ptr<ECS::ComponentManager> components {};
		std::uint64_t fileWatchHandle = 0;

		[[nodiscard]] ECS::ScriptComponent* resolve() const;
	};

	static TrackedScript capture(ECS::ScriptComponent& component);
	TrackedScript* findScript(const ECS::ComponentHandle<ECS::ScriptComponent>& handle);
	void pruneScripts();
	bool registerScript(TrackedScript& tracked, ECS::ScriptComponent& component);
	void unregisterScript(IKIGAI::ECS::ScriptComponent& component);
	bool reloadScript(TrackedScript& tracked, ECS::ScriptComponent& component);
	void attachFileWatch(TrackedScript& tracked, const ECS::ScriptComponent& component);
	static void detachFileWatch(TrackedScript& tracked);

	std::unique_ptr<sol::state> luaState;
	sol::table sharedBindings {};
	std::string scriptRootFolder;
	std::vector<TrackedScript> scripts;
	EVENT::EventListener mCreatedListener {};
	EVENT::EventListener mDestroyedListener {};
	bool checkOk = false;
};
}

#endif
