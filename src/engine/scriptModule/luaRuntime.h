#pragma once
#if USE_LUA
#include <sol/sol.hpp>
#include <string>

namespace IKIGAI::SCRIPTING {
class LuaRuntime {
public:
	explicit LuaRuntime(void (*installBindings)(sol::state&));
	sol::protected_function_result execute(const std::string& source, const std::string& chunkName, sol::environment& environment);

private:
	sol::state vm;
	sol::table sharedBindings {};
};
}
#endif
