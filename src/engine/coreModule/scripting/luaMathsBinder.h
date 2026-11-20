#if USE_LUA
#pragma once

namespace sol {
class state;
}

namespace IKIGAI::SCRIPTING {
class LuaMathsBinder {
public:
	static void BindMaths(sol::state& p_luaState);
};
}

#endif
