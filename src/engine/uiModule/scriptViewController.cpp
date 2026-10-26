#include "scriptModule/scriptSource.h"
#include "view.h"
#include <stdexcept>
#if USE_LUA
#include <sol/sol.hpp>
#endif
#if USE_IKIGAI_SCRIPT
#include <Library/ikigaiScript.h>
#endif

namespace IKIGAI::UI {
namespace {
	class ScriptController : public Controller {
	protected:
		std::shared_ptr<Controller> host;
		bool active = false;
		virtual Json call(const std::string& name, const std::vector<Json>& args) = 0;
		virtual void requireFunction(const std::string& name) = 0;
		Controller& native(bool mutate = false) {
			if (!host)
				throw std::runtime_error("This window has no native host");
			if (mutate && !active)
				throw std::runtime_error("Host mutation is forbidden while loading/validating a view");
			return *host;
		}

	public:
		explicit ScriptController(std::shared_ptr<Controller> api)
			: host(std::move(api)) { }
		void activate() override {
			active = true;
		}
		void exports(const Json& descriptor) {
			const auto props = descriptor.value("properties", Json::object());
			const auto cmds = descriptor.value("commands", Json::object());
			if (!props.is_object() || !cmds.is_object())
				throw std::runtime_error("Script properties/commands must be objects");
			for (const auto& [name, p] : props.items()) {
				const auto getter = p.at("get").get<std::string>();
				const auto setter = p.value("set", std::string());
				const auto commit = p.value("commit", std::string());
				requireFunction(getter);
				const auto type = ParseValueType(p.at("type").get<std::string>());
				const bool integerConditions = descriptor.at("backend") == "ikigai";
				Property property { type,
					[this, getter, type, integerConditions] {
						auto value = call(getter, { });
						// This interpreter represents relational results as Int(0/1).
						if (integerConditions && type == ValueType::Boolean && value.is_number_integer() && (value == 0 || value == 1))
							return Json(value == 1);
						return value;
					},
					{ }, { } };
				if (!setter.empty()) {
					requireFunction(setter);
					property.set = [this, setter](const Json& value) { call(setter, { value }); };
				}
				if (!commit.empty()) {
					requireFunction(commit);
					property.commit = [this, commit](const Json& before, const Json& after) { call(commit, { before, after }); };
				}
				properties.emplace(name, std::move(property));
			}
			for (const auto& [name, c] : cmds.items()) {
				auto function = c.at("call").get<std::string>();
				requireFunction(function);
				Arguments arguments;
				auto args = c.value("args", Json::object());
				if (!args.is_object())
					throw std::runtime_error("Command args must declare an object of types");
				for (const auto& [key, type] : args.items())
					arguments.emplace(key, ParseValueType(type.get<std::string>()));
				commands.emplace(name, Command { std::move(arguments), [this, function](const Json& args) { call(function, { args }); } });
			}
		}
	};

#if USE_LUA
	class LuaController final : public ScriptController {
		sol::state vm;
		sol::object toLua(const Json& value) {
			if (value.is_null())
				return sol::make_object(vm, sol::lua_nil);
			if (value.is_boolean())
				return sol::make_object(vm, value.get<bool>());
			if (value.is_number_integer())
				return sol::make_object(vm, value.get<int64_t>());
			if (value.is_number())
				return sol::make_object(vm, value.get<double>());
			if (value.is_string())
				return sol::make_object(vm, value.get<std::string>());
			if (!value.is_object())
				throw std::runtime_error("Expected scalar or command arguments");
			sol::table table = vm.create_table();
			for (const auto& [key, v] : value.items())
				table[key] = toLua(v);
			return sol::make_object(vm, table);
		}
		Json fromLua(const sol::object& value) {
			switch (value.get_type()) {
			case sol::type::lua_nil:
				return nullptr;
			case sol::type::boolean:
				return value.as<bool>();
			case sol::type::string:
				return value.as<std::string>();
			case sol::type::number:
				if (value.is<int64_t>())
					return value.as<int64_t>();
				return value.as<double>();
			default:
				throw std::runtime_error("UI values must be scalar");
			}
		}
		void requireFunction(const std::string& name) override {
			sol::object function = vm[name];
			if (function.get_type() != sol::type::function)
				throw std::runtime_error("Missing Lua function '" + name + "'");
		}
		Json call(const std::string& name, const std::vector<Json>& args) override {
			sol::protected_function function = vm[name];
			std::vector<sol::object> values;
			for (const auto& arg : args)
				values.push_back(toLua(arg));
			auto result = function(sol::as_args(values));
			if (!result.valid()) {
				sol::error error = result;
				throw std::runtime_error(name + ": " + error.what());
			}
			if (!result.return_count())
				return nullptr;
			return fromLua(result.get<sol::object>());
		}

	public:
		LuaController(const std::string& source, std::shared_ptr<Controller> host)
			: ScriptController(std::move(host)) {
			vm.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
			auto bridge = vm.create_named_table("Host");
			bridge.set_function("Get", [this](const std::string& name) { return toLua(native().get(name)); });
			bridge.set_function("Set", [this](const std::string& name, sol::object value) { native(true).set(name, fromLua(value)); });
			bridge.set_function("Call", [this](const std::string& name, sol::table args) {
				Json values = Json::object();
				for (const auto& pair : args) {
					if (pair.first.get_type() != sol::type::string)
						throw std::runtime_error("Command keys must be strings");
					values[pair.first.as<std::string>()] = fromLua(pair.second);
				}
				native(true).invoke(name, values);
			});
			auto result = vm.safe_script(SCRIPTING::NormalizeScriptSource(source), sol::script_pass_on_error);
			if (!result.valid()) {
				sol::error error = result;
				throw std::runtime_error(error.what());
			}
		}
	};
#endif

#if USE_IKIGAI_SCRIPT
	class IkController final : public ScriptController {
		// The interpreter retains source spans: source must outlive vm.
		std::string source;
		IkigaiScript::IkigaiScriptInterpreter vm;
		using V = IkigaiScript::ValuePtr;
		V toIk(const Json& value) {
			if (value.is_null())
				return vm.makeValue();
			if (value.is_boolean())
				return vm.makeValue(value.get<bool>());
			if (value.is_number_integer())
				return vm.makeValue(static_cast<IkigaiScript::Int>(value.get<int64_t>()));
			if (value.is_number())
				return vm.makeValue(static_cast<IkigaiScript::Float>(value.get<double>()));
			if (value.is_string())
				return vm.makeValue(value.get<std::string>());
			if (!value.is_object())
				throw std::runtime_error("Expected scalar or command arguments");
			auto map = std::make_shared<IkigaiScript::Dictionary>();
			for (const auto& [key, v] : value.items())
				map->emplace(vm.makeValue(key)->getHash(), toIk(v));
			return vm.makeValue(map);
		}
		Json fromIk(const V& value) {
			if (!value)
				return nullptr;
			switch (value->getType()) {
			case IkigaiScript::Type::Null:
				return nullptr;
			case IkigaiScript::Type::Bool:
				return value->getBool();
			case IkigaiScript::Type::Int:
				return value->getInt();
			case IkigaiScript::Type::Float:
				return value->getFloat();
			case IkigaiScript::Type::String:
				return value->getString();
			default:
				throw std::runtime_error("UI values must be scalar");
			}
		}
		std::string string(const V& v) {
			if (!v || v->getType() != IkigaiScript::Type::String)
				throw std::runtime_error("Expected string");
			return v->getString();
		}
		void requireFunction(const std::string& name) override {
			if (!vm.getGlobalScope()->functions.contains(name))
				throw std::runtime_error("Missing IkigaiScript function '" + name + "'");
		}
		Json call(const std::string& name, const std::vector<Json>& args) override {
			try {
				IkigaiScript::List values;
				for (const auto& arg : args)
					values.push_back(toIk(arg));
				auto result = vm.callFunction(name, values);
				if (vm.__EXEPTION__ != IkigaiScript::ExceptionType::None)
					throw std::runtime_error(vm.__DEBUG_OUT__);
				return fromIk(result);
			} catch (const IkigaiScript::Exception& e) {
				throw std::runtime_error(name + ": " + e.errStr);
			}
		}

	public:
		IkController(const std::string& input, std::shared_ptr<Controller> host)
			: ScriptController(std::move(host))
			, source(SCRIPTING::NormalizeScriptSource(input)) {
			auto bridge = vm.newModule("Host", 0, { });
			vm.newFunction("Get", bridge, IkigaiScript::Lambda([this](const IkigaiScript::List& args) {
				if (args.size() != 1)
					throw std::runtime_error("Host.Get expects a name");
				return toIk(native().get(string(args[0])));
			}));
			vm.newFunction("Set", bridge, IkigaiScript::Lambda([this](const IkigaiScript::List& args) {
				if (args.size() != 2)
					throw std::runtime_error("Host.Set expects name, value");
				native(true).set(string(args[0]), fromIk(args[1]));
				return vm.makeValue();
			}));
			vm.newFunction("Call", bridge, IkigaiScript::Lambda([this](const IkigaiScript::List& args) {
				if (args.size() != 2 || args[1]->getType() != IkigaiScript::Type::Map)
					throw std::runtime_error("Host.Call expects name, argument map");
				const auto name = string(args[0]);
				auto& api = native(true);
				auto& map = *args[1]->getDictionary();
				const auto& signature = api.commands.at(name).arguments;
				if (map.size() != signature.size())
					throw std::runtime_error("Wrong native argument count");
				Json values = Json::object();
				for (const auto& [key, type] : signature)
					values[key] = fromIk(map.at(vm.makeValue(key)->getHash()));
				api.invoke(name, values);
				return vm.makeValue();
			}));
			try {
				if (vm.evaluate(source) || vm.__EXEPTION__ != IkigaiScript::ExceptionType::None)
					throw std::runtime_error(vm.__DEBUG_OUT__);
			} catch (const IkigaiScript::Exception& e) {
				throw std::runtime_error(e.errStr);
			}
		}
		~IkController() override {
			vm.clearState();
		}
	};
#endif
}
std::shared_ptr<Controller> CreateScriptController(const Json& descriptor, const std::string& source, std::shared_ptr<Controller> host) {
	const auto backend = descriptor.at("backend").get<std::string>();
	std::shared_ptr<ScriptController> result;
	if (backend == "lua") {
#if USE_LUA
		result = std::make_shared<LuaController>(source, std::move(host));
#else
		throw std::runtime_error("Lua UI controller requested, but USE_LUA is disabled");
#endif
	}
	else if (backend == "ikigai") {
#if USE_IKIGAI_SCRIPT
		result = std::make_shared<IkController>(source, std::move(host));
#else
		throw std::runtime_error("IkigaiScript UI controller requested, but USE_IKIGAI_SCRIPT is disabled");
#endif
	}
	else
		throw std::runtime_error("Unknown UI controller backend '" + backend + "'");
	result->exports(descriptor);
	return result;
}
}
