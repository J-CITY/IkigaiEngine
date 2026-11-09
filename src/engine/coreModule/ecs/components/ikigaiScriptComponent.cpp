#if USE_IKIGAI_SCRIPT
#include "ikigaiScriptComponent.h"
#include "coreModule/ecs/object.h"
#include "scriptModule/ikigaiScriptRuntime.h"
#include "scriptModule/scriptFileWatcher.h"
#include "scriptModule/scriptVfsPath.h"
namespace IKIGAI::ECS {
IkigaiScriptComponent::IkigaiScriptComponent(UTILS::Ref<Object> object)
	: ComponentBase(object) {
	__NAME__ = "IkigaiScriptComponent";
}
IkigaiScriptComponent::IkigaiScriptComponent(UTILS::Ref<Object> object, const std::string& script)
	: IkigaiScriptComponent(object) {
	path = script;
}
IkigaiScriptComponent::IkigaiScriptComponent(UTILS::Ref<Object> object, const Descriptor& d)
	: IkigaiScriptComponent(object, d.Path) { }
IkigaiScriptComponent::IkigaiScriptComponent(const IkigaiScriptComponent& o)
	: ComponentBase(o)
	, path(o.path) { }
IkigaiScriptComponent& IkigaiScriptComponent::operator=(const IkigaiScriptComponent& o) {
	if (this != &o) {
		onDestroy();
		ComponentBase::operator=(o);
		path = o.path;
		awake = started = enabled = false;
	}
	return *this;
}
IkigaiScriptComponent::IkigaiScriptComponent(IkigaiScriptComponent&&) noexcept = default;
IkigaiScriptComponent& IkigaiScriptComponent::operator=(IkigaiScriptComponent&&) noexcept = default;
IkigaiScriptComponent::~IkigaiScriptComponent() {
	onDestroy();
}
IkigaiScriptComponent::Descriptor IkigaiScriptComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<IkigaiScriptComponent>();
	d.Path = path;
	return d;
}
bool IkigaiScriptComponent::isScriptOk() const {
	return runtime && runtime->isOk();
}
void IkigaiScriptComponent::attachFileWatch() {
#if defined(USE_FILE_WATCHER)
	if (fileWatchHandle || path.empty()) {
		return;
	}
	auto& world = obj->getWorld();
	auto* manager = world.getComponentManager();
	const auto handle = manager->getComponentHandle<IkigaiScriptComponent>(obj->getID());
	if (manager->resolveComponent(handle) != this) {
		// OnAwake may run on the temporary component before insertion.
		return;
	}
	const auto vfsPath = SCRIPTING::IkigaiScriptVfsPath(path);
	fileWatchHandle = SCRIPTING::ScriptFileWatcher::watch(vfsPath, [components = world.getComponentManagerWeak(), handle]() {
		if (const auto manager = components.lock()) {
			if (auto* script = manager->resolveComponent(handle)) {
				script->reloadScript();
			}
		}
	});
#endif
}

void IkigaiScriptComponent::detachFileWatch() {
#if defined(USE_FILE_WATCHER)
	if (fileWatchHandle) {
		SCRIPTING::ScriptFileWatcher::unwatch(*fileWatchHandle);
		fileWatchHandle.reset();
	}
#endif
}

void IkigaiScriptComponent::ensureRuntime() {
	if (runtime) {
		attachFileWatch();
		return;
	}
	if (!path.empty() && !obj.get().weak_from_this().expired()) {
		runtime = std::make_unique<SCRIPTING::IkigaiScriptRuntime>(obj.get(), path);
		attachFileWatch();
		if (awake) {
			runtime->call("OnAwake");
		}
		if (enabled) {
			runtime->call("OnEnable");
		}
		if (started) {
			runtime->call("OnStart");
		}
	}
}

void IkigaiScriptComponent::reloadScript() {
	if (path.empty() || obj.get().weak_from_this().expired()) {
		return;
	}
	if (!runtime) {
		ensureRuntime();
		return;
	}
	runtime->reload(obj.get());
	if (awake) {
		runtime->call("OnAwake");
	}
	if (enabled) {
		runtime->call("OnEnable");
	}
	if (started) {
		runtime->call("OnStart");
	}
}

void IkigaiScriptComponent::setScript(const std::string& script) {
	if (path == script) {
		reloadScript();
		return;
	}
	if (runtime) {
		if (enabled) {
			runtime->call("OnDisable");
		}
		runtime->call("OnDestroy");
		detachFileWatch();
		runtime.reset();
	}
	path = script;
	if (awake) {
		ensureRuntime();
	}
}
void IkigaiScriptComponent::onAwake() {
	if (awake) {
		return;
	}
	awake = true;
	ensureRuntime();
}
void IkigaiScriptComponent::onEnable() {
	onAwake();
	if (enabled) {
		return;
	}
	enabled = true;
	if (runtime) {
		runtime->call("OnEnable");
	}
}
void IkigaiScriptComponent::onStart() {
	onAwake();
	if (started) {
		return;
	}
	started = true;
	if (runtime) {
		runtime->call("OnStart");
	}
}
void IkigaiScriptComponent::onDisable() {
	if (!enabled) {
		return;
	}
	enabled = false;
	if (runtime) {
		runtime->call("OnDisable");
	}
}
void IkigaiScriptComponent::onDestroy() {
	if (runtime) {
		onDisable();
		runtime->call("OnDestroy");
		detachFileWatch();
		runtime.reset();
	}
	awake = started = false;
}
void IkigaiScriptComponent::onUpdate(std::chrono::duration<double> dt) {
	ensureRuntime();
	if (runtime) {
		runtime->call("OnUpdate", dt.count());
	}
}
void IkigaiScriptComponent::onFixedUpdate(std::chrono::duration<double> dt) {
	if (runtime) {
		runtime->call("OnFixedUpdate", dt.count());
	}
}
void IkigaiScriptComponent::onLateUpdate(std::chrono::duration<double> dt) {
	if (runtime) {
		runtime->call("OnLateUpdate", dt.count());
	}
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::IkigaiScriptComponent>();
	return true;
}();
}

#endif
