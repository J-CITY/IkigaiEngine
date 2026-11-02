#if USE_LUA
#pragma once
#include "../components/scriptComponent.h"

#include "ecsModule/systemManager.h"
#include "utilsModule/log/loggerDefine.h"

namespace sol {
class state;
class table;
}

namespace IKIGAI::ECS {
class Object;
}

namespace IKIGAI::ECS {
class ScriptSystem : public IKIGAI::ECS::System {
public:
	ScriptSystem();
	void onAwake(ECS::World& world) override;
	void onStart(ECS::World& world) override;
	void onEnable(ECS::World& world) override;
	void onDisable(ECS::World& world) override;
	void onDestroy(ECS::World& world) override;
	void onUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) override;
	void onFixedUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) override;
	void onLateUpdate(ECS::World& world, ECS::CommandBuffer& cb, std::chrono::duration<double> dt) override;

	bool registerToLuaContext(ScriptComponent& component, sol::state& luaState, const sol::table& sharedBindings);
	void unregisterFromLuaContext(ScriptComponent& component);
	void invokeOnReload(ScriptComponent& component);

private:
	template <typename... Args> inline void luaCall(ScriptComponent& component, const std::string& functionName, Args&&... args) {
		auto& object = component.getTable();
		if (object.valid()) {
			if (object[functionName].valid()) {
				sol::protected_function pfr = object[functionName];
				auto pfrResult = pfr.call(object, std::forward<Args>(args)...);
				if (!pfrResult.valid()) {
					sol::error err = pfrResult;
					LOG_ERROR << (err.what());
				}
			}
		}
	}
};
}
#endif
