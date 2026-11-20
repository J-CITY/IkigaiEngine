#include "cameraUiBindings.h"
#if USE_JSON_VIEWS && !defined(OCULUS)
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/object.h"
#include "sceneModule/sceneManager.h"
#include "uiModule/viewStore.h"
#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "editorModule/cameraTransformEdit.h"
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::CORE {
namespace {
	using UI::Arguments;
	using UI::Controller;
	using UI::Json;
	using UI::ValueType;

	MATH::Vector3f vectorArgs(const Json& args) {
		MATH::Vector3f value(args.at("x").get<float>(), args.at("y").get<float>(), args.at("z").get<float>());
		if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
			throw std::runtime_error("Camera arguments exceed float range");
		}
		return value;
	}
	std::shared_ptr<ECS::Object> cameraObject(SCENE_SYSTEM::SceneManager& manager, [[maybe_unused]] bool editable) {
		if (!manager.hasCurrentScene() || manager.isEditingLocked()) {
			return {};
		}
		auto camera = manager.getCurrentScene().findMainCamera();
		if (!camera || !camera->obj->isAlive()) {
			return {};
		}
		auto object = camera->obj->shared_from_this();
#ifdef USE_EDITOR
		if (editable && EDITOR::IsCameraTransformLocked(*object)) {
			return {};
		}
#endif
		return object;
	}
	void transform([[maybe_unused]] SCENE_SYSTEM::SceneManager& manager, const std::shared_ptr<ECS::Object>& object, MATH::Vector3f position,
		MATH::QuaternionF rotation) {
#ifdef USE_EDITOR
		EDITOR::RecordCameraTransform(object, manager.getCurrentScene().lifetime, position, rotation);
#else
		object->tryGetTransform()->setLocalPosition(position);
		object->tryGetTransform()->setLocalRotation(rotation);
#endif
	}
	std::shared_ptr<Controller> cameraController(SCENE_SYSTEM::SceneManager& manager) {
		auto controller = std::make_shared<Controller>();
		auto step = std::make_shared<double>(0.3);
		controller->properties.emplace("camera.speed",
			Controller::Property { ValueType::Number, [step] { return Json(*step); },
			[step](const Json& value) { *step = std::clamp(value.get<double>(), 0.01, 100.0); }, {} });
		controller->properties.emplace(
			"camera.canMove", Controller::Property { ValueType::Boolean, [&manager] { return Json(bool(cameraObject(manager, true))); }, {}, {} });
		controller->properties.emplace("camera.status",
			Controller::Property { ValueType::String,
			[&manager] {
				if (cameraObject(manager, true)) {
					return Json("Camera ready");
				}
				if (cameraObject(manager, false)) {
					return Json("Unpack prefab to move its internal camera");
				}
				return Json("No editable main camera");
			},
			{}, {} });
		const Arguments axes { { "x", ValueType::Number }, { "y", ValueType::Number }, { "z", ValueType::Number } };
		controller->commands.emplace(
			"camera.move", Controller::Command { axes, [&manager, step](const Json& args) {
													auto camera = cameraObject(manager, true);
													if (!camera) {
														throw std::runtime_error("No editable main camera");
													}
													auto position = camera->tryGetTransform()->getLocalPosition();
													position += vectorArgs(args) * float(*step);
													if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
														throw std::runtime_error("Camera position exceeds float range");
													}
													transform(manager, camera, position, camera->tryGetTransform()->getLocalRotation());
												} });
		controller->commands.emplace(
			"camera.rotate", Controller::Command { axes, [&manager](const Json& args) {
													  auto camera = cameraObject(manager, true);
													  if (!camera) {
														  throw std::runtime_error("No editable main camera");
													  }
													  const MATH::QuaternionF rotation(vectorArgs(args));
													  // Match TransformComponent::rotateLocal composition.
													  auto old = camera->tryGetTransform()->getLocalRotation();
													  auto next = old * rotation;
													  transform(manager, camera, camera->tryGetTransform()->getLocalPosition(), next);
												  } });
		controller->commands.emplace("debug.log", Controller::Command { { { "message", ValueType::String } }, [](const Json& args) {
																		   LOG_INFO << args.at("message").get<std::string>();
																	   } });
		return controller;
	}
}
void RegisterCameraUiBindings(UI::ViewStore& store, SCENE_SYSTEM::SceneManager& manager) {
	store.factories["camera"] = [&manager] { return cameraController(manager); };
}
}
#endif
