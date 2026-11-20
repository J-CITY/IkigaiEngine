#if USE_LUA
#pragma once

namespace sol {
class state;
}

namespace IKIGAI::SCRIPTING {
class LuaGlobalsBinder {
public:
	static void BindGlobals(sol::state& p_luaState);
};
}

#endif
