#if USE_IKIGAI_SCRIPT
#include "ikigaiScriptBinder.h"
#include "coreModule/ecs/components/ambientLight.h"
#include "coreModule/ecs/components/ambientSphereLight.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/directionalLight.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/pointLight.h"
#include "coreModule/ecs/components/spotLight.h"
#include "coreModule/ecs/object.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/log/loggerDefine.h"
#include "windowModule/inputManager/inputActions.h"
#include "windowModule/inputManager/inputManager.h"
#include "windowModule/window/window.h"
#include <magic_enum.hpp>
#if defined(USE_EDITOR) && !defined(OCULUS)
#include "imgui.h"
#endif

namespace magic_enum::customize {
template <> struct enum_range<IKIGAI::INPUT_SYSTEM::EKey> {
	static constexpr int min = -1, max = 512;
};
}

namespace IKIGAI::SCRIPTING {
using namespace IkigaiScript;
using namespace IKIGAI::MATH;
using namespace IKIGAI::ECS;
using namespace IKIGAI::INPUT_SYSTEM;
using namespace IKIGAI::RESOURCES;
using namespace IKIGAI::SCENE_SYSTEM;

IkigaiScriptBinder::IkigaiScriptBinder(IkigaiScriptInterpreter& interpreter)
	: vm(interpreter) {
	bindMaths();
	bindComponents();
	bindActor();
	bindGlobals();
}
IkigaiScriptBinder::V IkigaiScriptBinder::object(Object& owner) {
	auto ref = owner.weak_from_this();
	for (auto& h : handles)
		if (h.type == typeid(Object)) {
			auto& previous = std::any_cast<std::weak_ptr<Object>&>(h.value);
			if (!previous.owner_before(ref) && !ref.owner_before(previous))
				return h.script;
		}
	auto c = std::make_shared<C>(vm.resolveScope("Object"));
	c->variables["__handle"] = value(handles.size());
	auto result = classValue(c);
	handles.push_back({ typeid(Object), ref, result });
	return result;
}
void IkigaiScriptBinder::bindActor() {
	type<Object>("Object");
	member("GetName", &Object::getName);
	member("SetName", &Object::setName);
	member("GetTag", &Object::getTag);
	member("SetTag", &Object::setTag);
	member("GetID", &Object::getIDInt);
	member("IsSelfActive", &Object::getIsSelfActive);
	member("IsActive", &Object::getIsActive);
	member("SetActive", &Object::setActive);
	member("Destroy", &Object::markAsDestroy);
	member("DetachFromParent", &Object::detachFromParent);
	method<Object>("GetParent", [this](Object& o, const Args&) {
		auto p = o.getParent();
		return p ? object(*p) : vm.makeValue();
	});
	method<Object>("GetChildren", [this](Object& o, const Args&) {
		List list;
		for (auto& child : o.getChildren())
			list.push_back(object(*child));
		return vm.makeValue(list);
	});
	method<Object>("SetParent", [this](Object& o, const Args& a) {
		o.setParent(reference<Object>(a.at(0))->shared_from_this());
		return vm.makeValue();
	});
#define GET_COMPONENT(Name, T) method<Object>("Get" Name, [this](Object& o, const Args&) { return component<T>(o.getWorld(), o.getID()); })
	GET_COMPONENT("Transform", TransformComponent);
	GET_COMPONENT("Camera", CameraComponent);
	GET_COMPONENT("PointLight", PointLight);
	GET_COMPONENT("SpotLight", SpotLight);
	GET_COMPONENT("DirectionalLight", DirectionalLight);
	GET_COMPONENT("AmbientBoxLight", AmbientLight);
	GET_COMPONENT("AmbientSphereLight", AmbientSphereLight);
	GET_COMPONENT("ModelRenderer", ModelRenderer);
	GET_COMPONENT("MaterialRenderer", MaterialRenderer);
#undef GET_COMPONENT
#define REMOVE_COMPONENT(Name, T) method<Object>("Remove" Name, [this](Object& o, const Args&) { return value(o.removeComponent<T>()); })
	REMOVE_COMPONENT("Camera", CameraComponent);
	REMOVE_COMPONENT("ModelRenderer", ModelRenderer);
	REMOVE_COMPONENT("MaterialRenderer", MaterialRenderer);
	REMOVE_COMPONENT("PointLight", PointLight);
	REMOVE_COMPONENT("SpotLight", SpotLight);
	REMOVE_COMPONENT("DirectionalLight", DirectionalLight);
	REMOVE_COMPONENT("AmbientBoxLight", AmbientLight);
	REMOVE_COMPONENT("AmbientSphereLight", AmbientSphereLight);
#undef REMOVE_COMPONENT
	method<Object>("AddTransform", [this](Object& o, const Args&) { return component(o.addComponent<TransformComponent>()); });
}
void IkigaiScriptBinder::bindComponents() {
	type<TransformComponent>("Transform");
	type<CameraComponent>("Camera");
	type<ModelRenderer>("ModelRenderer");
	type<MaterialRenderer>("MaterialRenderer");
	type<PointLight>("PointLight");
	type<SpotLight>("SpotLight");
	type<DirectionalLight>("DirectionalLight");
	type<AmbientLight>("AmbientBoxLight");
	type<AmbientSphereLight>("AmbientSphereLight");
	member("SetPosition", &TransformComponent::setLocalPosition);
	member("SetRotation", &TransformComponent::setLocalRotation);
	member("SetScale", &TransformComponent::setLocalScale);
	member("SetLocalPosition", &TransformComponent::setLocalPosition);
	member("SetLocalRotation", &TransformComponent::setLocalRotation);
	member("SetLocalScale", &TransformComponent::setLocalScale);
	member("GetPosition", &TransformComponent::getWorldPosition);
	member("GetRotation", &TransformComponent::getWorldRotation);
	member("GetScale", &TransformComponent::getWorldScale);
	member("GetLocalPosition", &TransformComponent::getLocalPosition);
	member("GetLocalRotation", &TransformComponent::getLocalRotation);
	member("GetLocalScale", &TransformComponent::getLocalScale);
	member("GetWorldPosition", &TransformComponent::getWorldPosition);
	member("GetWorldRotation", &TransformComponent::getWorldRotation);
	member("GetWorldScale", &TransformComponent::getWorldScale);
	member("GetForward", &TransformComponent::getWorldForward);
	member("GetRight", &TransformComponent::getWorldRight);
	member("GetUp", &TransformComponent::getWorldUp);
	member("GetLocalForward", &TransformComponent::getLocalForward);
	member("GetLocalRight", &TransformComponent::getLocalRight);
	member("GetLocalUp", &TransformComponent::getLocalUp);
	member("GetWorldForward", &TransformComponent::getWorldForward);
	member("GetWorldRight", &TransformComponent::getWorldRight);
	member("GetWorldUp", &TransformComponent::getWorldUp);
	member("GetFov", &CameraComponent::getFov);
	member("SetFov", &CameraComponent::setFov);
	member("GetNear", &CameraComponent::getNear);
	member("SetNear", &CameraComponent::setNear);
	member("GetFar", &CameraComponent::getFar);
	member("SetFar", &CameraComponent::setFar);
	member("GetSize", &CameraComponent::getSize);
	member("SetSize", &CameraComponent::setSize);
	member("GetProjectionMode", &CameraComponent::getProjectionMode);
	member("SetProjectionMode", &CameraComponent::setProjectionMode);
	member("HasFrustumGeometryCulling", &CameraComponent::isFrustumGeometryCulling);
	member("SetFrustumGeometryCulling", &CameraComponent::setFrustumGeometryCulling);
	member("HasFrustumLightCulling", &CameraComponent::isFrustumLightCulling);
	member("SetFrustumLightCulling", &CameraComponent::setFrustumLightCulling);
	method<ModelRenderer>("GetModel", [this](ModelRenderer& r, const Args&) { return owned(r.getModel()); });
	method<ModelRenderer>("SetModel", [this](ModelRenderer& r, const Args& a) {
		r.setModel(resource<RENDER::ModelInterface>(a.at(0)));
		return vm.makeValue();
	});
	member("GetFrustumBehaviour", &ModelRenderer::getFrustumBehaviour);
	member("SetFrustumBehaviour", &ModelRenderer::setFrustumBehaviour);
	method<MaterialRenderer>("SetMaterial", [this](MaterialRenderer& r, const Args& a) {
		r.setMaterial(read<unsigned>(a.at(0)), resource<RENDER::MaterialInterface>(a.at(1)));
		return vm.makeValue();
	});
#define LIGHT(T)                                                                                                                                     \
	member("GetConstant", &T::getConstant);                                                                                                          \
	member("GetLinear", &T::getLinear);                                                                                                              \
	member("GetQuadratic", &T::getQuadratic);                                                                                                        \
	member("SetConstant", &T::setConstant);                                                                                                          \
	member("SetLinear", &T::setLinear);                                                                                                              \
	member("SetQuadratic", &T::setQuadratic);
	LIGHT(PointLight)
	LIGHT(SpotLight)
#undef LIGHT
	member("GetCutOff", &SpotLight::getCutoff);
	member("SetCutOff", &SpotLight::setCutoff);
	member("GetOuterCutOff", &SpotLight::getOuterCutoff);
	member("SetOuterCutOff", &SpotLight::setOuterCutoff);
	member("GetSize", &AmbientLight::getSize);
	member("SetSize", &AmbientLight::setSize);
	member("GetRadius", &AmbientSphereLight::getRadius);
	member("SetRadius", &AmbientSphereLight::setRadius);
}
void IkigaiScriptBinder::bindGlobals() {
	function("Math", "AsinDegrees",
		[this](const Args& a) { return value(std::asin(std::clamp(read<float>(a.at(0)), -1.0f, 1.0f)) * float(180.0 / 3.141592653589793)); });
	type<Scene>("Scene");
	type<RENDER::ModelInterface>("Model");
	type<RENDER::MaterialInterface>("Material");
	auto enumScope = [this](const char* name, auto sample, const std::string& prefix) {
		using E = decltype(sample);
		auto scope = vm.newModule(name, 0, { });
		for (auto [e, label] : magic_enum::enum_entries<E>()) {
			auto key = std::string(label);
			if (key.starts_with(prefix))
				key.erase(0, prefix.size());
			scope->insertVar(key, value(e));
		}
	};
	enumScope("Key", EKey { }, "KEY_");
	enumScope("MouseButton", EMouseButton { }, "MOUSE_");
	// GLFW and Quest use aliases; enum reflection reports only canonical names.
	auto* buttons = vm.findModuleByName("MouseButton");
	buttons->scope->insertVar("BUTTON_LEFT", value(EMouseButton::MOUSE_BUTTON_LEFT));
	buttons->scope->insertVar("BUTTON_RIGHT", value(EMouseButton::MOUSE_BUTTON_RIGHT));
	buttons->scope->insertVar("BUTTON_MIDDLE", value(EMouseButton::MOUSE_BUTTON_MIDDLE));
	enumScope("ProjectionMode", RENDER::Camera::ProjectionMode { }, "");
	enumScope("FrustumBehaviour", EFrustumBehaviour { }, "");
	function("Inputs", "GetKeyDown", [this](const Args& a) { return value(ServiceManager::Get<InputManager>().isKeyPressed(read<EKey>(a.at(0)))); });
	function("Inputs", "GetKeyUp", [this](const Args& a) { return value(ServiceManager::Get<InputManager>().isKeyReleased(read<EKey>(a.at(0)))); });
	function("Inputs", "GetMouseButtonDown",
		[this](const Args& a) { return value(ServiceManager::Get<InputManager>().isMouseButtonPressed(read<EMouseButton>(a.at(0)))); });
	function("Inputs", "GetMouseButtonUp",
		[this](const Args& a) { return value(ServiceManager::Get<InputManager>().isMouseButtonReleased(read<EMouseButton>(a.at(0)))); });
	function("Inputs", "GetMousePos", [this](const Args&) {
		auto p = ServiceManager::Get<InputManager>().getMousePosition();
		return value(Vector2f(float(p.x), float(p.y)));
	});
	function("Inputs", "HasFocus", [this](const Args&) { return value(ServiceManager::Get<WINDOW::Window>().hasFocus()); });
	function("Inputs", "CanControlCamera", [this](const Args&) {
		bool allowed = ServiceManager::Get<WINDOW::Window>().hasFocus();
#if defined(USE_EDITOR) && !defined(OCULUS)
		if (ImGui::GetCurrentContext())
			allowed = allowed && !ImGui::GetIO().WantCaptureKeyboard && !ImGui::GetIO().WantCaptureMouse;
#endif
		return value(allowed);
	});
	function(
		"Inputs", "IsPressed", [this](const Args& a) { return value(ServiceManager::Get<InputActions>().isPressed(read<std::string>(a.at(0)))); });
	function("Inputs", "WasPressedThisFrame",
		[this](const Args& a) { return value(ServiceManager::Get<InputActions>().wasPressedThisFrame(read<std::string>(a.at(0)))); });
	function("Inputs", "WasReleasedThisFrame",
		[this](const Args& a) { return value(ServiceManager::Get<InputActions>().wasReleasedThisFrame(read<std::string>(a.at(0)))); });
	function("Inputs", "WasPressed",
		[this](const Args& a) { return value(ServiceManager::Get<InputActions>().wasPressedThisFrame(read<std::string>(a.at(0)))); });
	function("Inputs", "ReadValue", [this](const Args& a) -> V {
		auto& actions = ServiceManager::Get<InputActions>();
		auto action = read<std::string>(a.at(0));
		auto v = actions.readValue2(action);
		if (v.x != 0 || v.y != 0)
			return value(v);
		return value(actions.readValue(action));
	});
	function("Scenes", "GetCurrentScene", [this](const Args&) {
		auto& manager = ServiceManager::Get<SceneManager>();
		if (!manager.hasCurrentScene())
			return vm.makeValue();
		auto* scene = &manager.getCurrentScene();
		auto result = owned(std::shared_ptr<Scene>(scene, [](Scene*) { }));
		auto id = read<size_t>(result->getClass()->variables.at("__handle"));
		handles.at(id).alive = [scene] {
			if (!ServiceManager::Check<SceneManager>())
				return false;
			auto& m = ServiceManager::Get<SceneManager>();
			return m.hasCurrentScene() && &m.getCurrentScene() == scene;
		};
		return result;
	});
	method<Scene>("FindActorByName", [this](Scene& s, const Args& a) {
		auto o = s.findObjectByName(read<std::string>(a.at(0)));
		return o ? object(*o) : vm.makeValue();
	});
	method<Scene>("FindActorByTag", [this](Scene& s, const Args& a) {
		auto o = s.findObjectByTag(read<std::string>(a.at(0)));
		return o ? object(*o) : vm.makeValue();
	});
	method<Scene>("FindActorsByName", [this](Scene& s, const Args& a) {
		List v;
		for (auto& o : s.findObjectsByName(read<std::string>(a.at(0))))
			v.push_back(object(*o));
		return vm.makeValue(v);
	});
	method<Scene>("FindActorsByTag", [this](Scene& s, const Args& a) {
		List v;
		for (auto& o : s.findObjectsByTag(read<std::string>(a.at(0))))
			v.push_back(object(*o));
		return vm.makeValue(v);
	});
	function("Resources", "GetModel", [this](const Args& a) {
		return owned(ServiceManager::Get<ModelLoader>().CreateFromFile(read<std::string>(a.at(0)), ModelParserFlags::NONE));
	});
	function("Resources", "GetMaterial",
		[this](const Args& a) { return owned(ServiceManager::Get<MaterialLoader>().Create(read<std::string>(a.at(0)))); });
	function("Debug", "Assert", [this](const Args& a) {
		if (!read<bool>(a.at(0)))
			throw std::runtime_error(read<std::string>(a.at(1)));
		return vm.makeValue();
	});
	function("Debug", "LogInfo", [this](const Args& a) {
		LOG_INFO << read<std::string>(a.at(0));
		return vm.makeValue();
	});
	function("Debug", "LogWarning", [this](const Args& a) {
		LOG_WARNING << read<std::string>(a.at(0));
		return vm.makeValue();
	});
	function("Debug", "LogError", [this](const Args& a) {
		LOG_ERROR << read<std::string>(a.at(0));
		return vm.makeValue();
	});
	function("Debug", "Log", [this](const Args& a) {
		LOG_INFO << read<std::string>(a.at(0));
		return vm.makeValue();
	});
}
}
#endif
