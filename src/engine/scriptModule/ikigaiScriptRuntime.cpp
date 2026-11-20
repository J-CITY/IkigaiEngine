#if USE_IKIGAI_SCRIPT
#include "ikigaiScriptRuntime.h"
#include "scriptVfsPath.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/stringUtils.h"
#include <Library/exception.h>
#include <Library/ikigaiScript.h>

namespace IKIGAI::SCRIPTING {
namespace {
	void reportScriptError(const std::string& path, const char* callback, const std::string& reason) {
		const auto message = "IkigaiScript '" + path + "' " + callback + ": " + reason;
		LOG_ERROR << message;
		IKIGAI_COUT(message);
	}
}

struct IkigaiScriptRuntime::State {
	std::string source;
	IkigaiScript::IkigaiScriptInterpreter vm;
	// Native functions can refer to their binding object until the VM is cleared.
	std::shared_ptr<void> bindings;
	~State() {
		vm.clearState();
	}
};

bool IkigaiScriptRuntime::loadFromVfs(const InstallBindings& installBindings) {
	healthy = true;
	try {
		state = std::make_unique<State>();
		state->bindings = installBindings(state->vm);

		const auto filename = IkigaiScriptVfsPath(path);
		const auto source = readText(filename);
		if (!source) {
			throw std::runtime_error("Cannot open " + filename);
		}
		state->source = UTILS::NormalizeTextInput(*source);
		healthy = !state->vm.evaluate(state->source) && state->vm.__EXEPTION__ == IkigaiScript::ExceptionType::None;
		if (!healthy) {
			throw std::runtime_error(
				state->vm.__DEBUG_OUT__.empty() ? "Script evaluation failed (see interpreter diagnostic)" : state->vm.__DEBUG_OUT__);
		}
		return true;
	} catch (const IkigaiScript::Exception& e) {
		reportScriptError(path, "load", e.errStr);
		healthy = false;
	} catch (const std::exception& e) {
		reportScriptError(path, "load", e.what());
		healthy = false;
	}
	return false;
}

IkigaiScriptRuntime::IkigaiScriptRuntime(const std::string& script, ReadScriptText& readText, const InstallBindings& installBindings)
	: path(script)
	, readText(readText) {
	loadFromVfs(installBindings);
}
IkigaiScriptRuntime::~IkigaiScriptRuntime() = default;

bool IkigaiScriptRuntime::reload(const InstallBindings& installBindings) {
	return loadFromVfs(installBindings);
}

void IkigaiScriptRuntime::call(const char* method) {
	if (!healthy) {
		return;
	}
	auto& functions = state->vm.getGlobalScope()->functions;
	auto it = functions.find(method);
	if (it == functions.end()) {
		return;
	}
	auto fn = it->second;
	try {
		state->vm.callFunction(fn, {});
		if (state->vm.__EXEPTION__ != IkigaiScript::ExceptionType::None) {
			throw std::runtime_error(
				state->vm.__DEBUG_OUT__.empty() ? "Script evaluation failed (see interpreter diagnostic)" : state->vm.__DEBUG_OUT__);
		}
	} catch (const IkigaiScript::Exception& e) {
		healthy = false;
		reportScriptError(path, method, e.errStr);
	} catch (const std::exception& e) {
		healthy = false;
		reportScriptError(path, method, e.what());
	}
}

void IkigaiScriptRuntime::call(const char* method, double dt) {
	if (!healthy) {
		return;
	}
	auto& functions = state->vm.getGlobalScope()->functions;
	auto it = functions.find(method);
	if (it == functions.end()) {
		return;
	}
	auto fn = it->second;
	try {
		state->vm.callFunction(fn, { state->vm.makeValue(static_cast<IkigaiScript::Float>(dt)) });
		if (state->vm.__EXEPTION__ != IkigaiScript::ExceptionType::None) {
			throw std::runtime_error(
				state->vm.__DEBUG_OUT__.empty() ? "Script evaluation failed (see interpreter diagnostic)" : state->vm.__DEBUG_OUT__);
		}
	} catch (const IkigaiScript::Exception& e) {
		healthy = false;
		reportScriptError(path, method, e.errStr);
	} catch (const std::exception& e) {
		healthy = false;
		reportScriptError(path, method, e.what());
	}
}
}
#endif
