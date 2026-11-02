#pragma once
#if USE_IKIGAI_SCRIPT
#include <memory>
#include <string>
namespace IKIGAI::ECS {
class Object;
}
namespace IKIGAI::SCRIPTING {
class IkigaiScriptRuntime {
	struct State;
	std::unique_ptr<State> state;
	std::string path;
	bool healthy = false;

	bool loadFromVfs(ECS::Object& owner);

public:
	IkigaiScriptRuntime(ECS::Object& owner, const std::string& script);
	~IkigaiScriptRuntime();
	bool reload(ECS::Object& owner);
	bool isOk() const {
		return healthy;
	}
	void call(const char* method);
	void call(const char* method, double dt);
};
}
#endif
