#include "luaRuntime.h"

#if USE_LUA
#include "utilsModule/stringUtils.h"

namespace IKIGAI::SCRIPTING {
LuaRuntime::LuaRuntime(ReadScriptText& readText, void (*installBindings)(sol::state&))
	: readText(readText) {
	vm.open_libraries(sol::lib::base, sol::lib::math);
	installBindings(vm);
	sharedBindings = vm.globals();
}

std::optional<std::string> LuaRuntime::loadSource(const std::string& path) const {
	const auto source = readText(path);
	if (!source) {
		return std::nullopt;
	}
	return UTILS::NormalizeTextInput(*source);
}

sol::protected_function_result LuaRuntime::execute(const std::string& source, const std::string& chunkName, sol::environment& environment) {
	environment = sol::environment(vm, sol::create, sharedBindings);
	return vm.safe_script(source, environment, sol::script_pass_on_error, chunkName);
}
}
#endif
