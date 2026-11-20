#pragma once
#if USE_IKIGAI_SCRIPT
#include "scriptSource.h"
#include <functional>
#include <memory>
#include <string>
namespace IkigaiScript {
class IkigaiScriptInterpreter;
}
namespace IKIGAI::SCRIPTING {
class IkigaiScriptRuntime {
public:
	using InstallBindings = std::function<std::shared_ptr<void>(IkigaiScript::IkigaiScriptInterpreter&)>;

private:
	struct State;
	std::unique_ptr<State> state;
	std::string path;
	ReadScriptText& readText;
	bool healthy = false;

	bool loadFromVfs(const InstallBindings& installBindings);

public:
	IkigaiScriptRuntime(const std::string& script, ReadScriptText& readText, const InstallBindings& installBindings);
	~IkigaiScriptRuntime();
	bool reload(const InstallBindings& installBindings);
	bool isOk() const {
		return healthy;
	}
	void call(const char* method);
	void call(const char* method, double dt);
};
}
#endif
