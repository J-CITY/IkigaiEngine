#include "luaRuntime.h"

#if USE_LUA
namespace IKIGAI::SCRIPTING {
LuaRuntime::LuaRuntime(void (*installBindings)(sol::state&)) {
	vm.open_libraries(sol::lib::base, sol::lib::math);
	installBindings(vm);
	sharedBindings = vm.globals();
}

sol::protected_function_result LuaRuntime::execute(const std::string& source, const std::string& chunkName, sol::environment& environment) {
	environment = sol::environment(vm, sol::create, sharedBindings);
	return vm.safe_script(source, environment, sol::script_pass_on_error, chunkName);
}
}
#endif
