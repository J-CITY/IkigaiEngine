#if USE_LUA
#pragma once

namespace sol {
class state;
}

namespace IKIGAI::SCRIPTING {
class LuaActorBinder {
public:
	static void BindActor(sol::state& p_luaState);
};
}

#endif
