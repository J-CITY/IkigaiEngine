#if USE_LUA
#pragma once

namespace sol {
class state;
}

namespace IKIGAI::SCRIPTING {
class LuaBinder {
public:
	static void CallBinders(sol::state& p_luaState);
};
}

#endif
