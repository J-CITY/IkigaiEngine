#pragma once
#if USE_LUA
#include "scriptSource.h"
#include <sol/sol.hpp>
#include <string>

namespace IKIGAI::SCRIPTING {
class LuaRuntime {
public:
	LuaRuntime(ReadScriptText& readText, void (*installBindings)(sol::state&));
	std::optional<std::string> loadSource(const std::string& path) const;
	sol::protected_function_result execute(const std::string& source, const std::string& chunkName, sol::environment& environment);

private:
	ReadScriptText& readText;
	sol::state vm;
	sol::table sharedBindings {};
};
}
#endif
