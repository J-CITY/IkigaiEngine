#if USE_IKIGAI_SCRIPT
#include "ikigaiScriptRuntime.h"
#include "ikigaiScriptBinder.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "scriptSource.h"
#include "scriptVfsPath.h"
#include "utilsModule/log/loggerDefine.h"
#include <Library/exception.h>

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
	IkigaiScriptBinder bindings { vm };
	~State() {
		vm.clearState();
	}
};

bool IkigaiScriptRuntime::loadFromVfs(ECS::Object& owner) {
	healthy = true;
	try {
		if (!state) {
			state = std::make_unique<State>();
		}
		else {
			state->source.clear();
			state->vm.clearState();
		}
		state->vm.getGlobalScope()->insertVar("owner", state->bindings.object(owner));

		const auto filename = IkigaiScriptVfsPath(path);
		auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(filename);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Cannot open " + filename);
		}
		state->source = NormalizeScriptSource(file->readStr());
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

IkigaiScriptRuntime::IkigaiScriptRuntime(ECS::Object& owner, const std::string& script)
	: path(script) {
	loadFromVfs(owner);
}
IkigaiScriptRuntime::~IkigaiScriptRuntime() = default;

bool IkigaiScriptRuntime::reload(ECS::Object& owner) {
	state = std::make_unique<State>();
	return loadFromVfs(owner);
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
