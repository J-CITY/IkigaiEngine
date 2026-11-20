#if USE_LUA
#pragma once

namespace sol {
class state;
}

namespace IKIGAI::SCRIPTING {
class LuaComponentBinder {
public:
	static void BindComponent(sol::state& p_luaState);
};
}

#endif
