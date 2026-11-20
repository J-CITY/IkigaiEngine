#include "contextPredicate.h"
#if USE_IKIGAI_SCRIPT
#include "utilsModule/stringUtils.h"
#include <Library/ikigaiScript.h>
#endif

namespace IKIGAI::SCRIPTING {
struct ContextPredicate::State {
#if USE_IKIGAI_SCRIPT
	IkigaiScript::IkigaiScriptInterpreter vm;
	IkigaiScript::FunctionRef function;
	~State() {
		function.reset();
		vm.clearState();
	}
#endif
};
ContextPredicate::ContextPredicate(const std::string& path, const std::string& name, ReadScriptText& readText)
	: state(std::make_unique<State>()) {
#if USE_IKIGAI_SCRIPT
	const auto source = readText(path);
	if (!source) {
		throw std::runtime_error("Cannot read predicate script: " + path);
	}
	if (state->vm.evaluate(UTILS::NormalizeTextInput(*source)) || state->vm.__EXEPTION__ != IkigaiScript::ExceptionType::None) {
		throw std::runtime_error("Predicate script compilation failed: " + path + ": " + state->vm.__DEBUG_OUT__);
	}
	const auto& functions = state->vm.getGlobalScope()->functions;
	auto fn = functions.find(name);
	if (fn == functions.end()) {
		throw std::runtime_error("Missing predicate function: " + name);
	}
	state->function = fn->second;
#else
	(void)path;
	(void)name;
	(void)readText;
	throw std::runtime_error("Predicate scripts require USE_IKIGAI_SCRIPT");
#endif
}
ContextPredicate::~ContextPredicate() = default;
bool ContextPredicate::evaluate(const std::vector<DATA::Value>& values) {
#if USE_IKIGAI_SCRIPT
	IkigaiScript::List args;
	args.reserve(values.size());
	for (const auto& v : values) {
		if (auto p = std::get_if<float>(&v))
			args.push_back(state->vm.makeValue(static_cast<IkigaiScript::Float>(*p)));
		else if (auto p = std::get_if<int>(&v))
			args.push_back(state->vm.makeValue(static_cast<IkigaiScript::Int>(*p)));
		else if (auto p = std::get_if<bool>(&v))
			args.push_back(state->vm.makeValue(*p));
		else
			throw std::runtime_error("Predicate arguments must be scalar");
	}
	auto result = state->vm.callFunction(state->function, args);
	if (state->vm.__EXEPTION__ != IkigaiScript::ExceptionType::None)
		throw std::runtime_error("Predicate execution failed: " + state->vm.__DEBUG_OUT__);
	// Comparison operators in IkigaiScript currently return Int(0/1).
	if (!result || (result->getType() != IkigaiScript::Type::Bool && result->getType() != IkigaiScript::Type::Int))
		throw std::runtime_error("Predicate must return Bool");
	if (result->getType() == IkigaiScript::Type::Int && result->getInt() != 0 && result->getInt() != 1)
		throw std::runtime_error("Predicate returned a non-boolean integer");
	return result->getBool();
#else
	(void)values;
	throw std::runtime_error("Predicate scripts are disabled");
#endif
}
}
