#include "viewHost.h"
#if USE_JSON_VIEWS && !defined(OCULUS)
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/object.h"
#include "resourceModule/serviceManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/log/loggerDefine.h"
#ifdef USE_EDITOR
#include "editorModule/editorRender.h"
#include "editorModule/prefabEditor.h"
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace IKIGAI::UI {
namespace {
	using namespace IKIGAI;
	MATH::Vector3f vectorArgs(const Json& args) {
		MATH::Vector3f value(args.at("x").get<float>(), args.at("y").get<float>(), args.at("z").get<float>());
		if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
			throw std::runtime_error("Camera arguments exceed float range");
		}
		return value;
	}
	std::shared_ptr<ECS::Object> cameraObject(bool editable) {
		auto& manager = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>();
		if (!manager.hasCurrentScene() || manager.isEditingLocked()) {
			return {};
		}
		auto camera = manager.getCurrentScene().findMainCamera();
		if (!camera || !camera->obj->isAlive()) {
			return {};
		}
		auto object = camera->obj->shared_from_this();
#ifdef USE_EDITOR
		if (editable && EDITOR::PREFABS::Locked(*object)) {
			return {};
		}
#endif
		return object;
	}
#ifdef USE_EDITOR
	class CameraTransformCommand final : public EDITOR::ICommand {
		std::weak_ptr<ECS::Object> object;
		std::weak_ptr<void> scene;
		MATH::Vector3f beforePosition, afterPosition;
		MATH::QuaternionF beforeRotation, afterRotation;
		void apply(bool undoing) {
			auto target = object.lock();
			if (scene.expired() || !target || !target->isAlive() || EDITOR::PREFABS::Locked(*target)) {
				return;
			}
			target->tryGetTransform()->setLocalPosition(undoing ? beforePosition : afterPosition);
			target->tryGetTransform()->setLocalRotation(undoing ? beforeRotation : afterRotation);
		}

	public:
		CameraTransformCommand(const std::shared_ptr<ECS::Object>& target, MATH::Vector3f position, MATH::QuaternionF rotation)
			: object(target)
			, scene(RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene().lifetime)
			, beforePosition(target->tryGetTransform()->getLocalPosition())
			, afterPosition(position)
			, beforeRotation(target->tryGetTransform()->getLocalRotation())
			, afterRotation(rotation) { }
		void execute() override {
			apply(false);
		}
		void undo() override {
			apply(true);
		}
		std::string getName() const override {
			return "Camera tools transform";
		}
	};
#endif
	void transform(const std::shared_ptr<ECS::Object>& object, MATH::Vector3f position, MATH::QuaternionF rotation) {
#ifdef USE_EDITOR
		EDITOR::EditorRender::GlobalState.mCommandHistory.addCommand(std::make_unique<CameraTransformCommand>(object, position, rotation));
#else
		object->tryGetTransform()->setLocalPosition(position);
		object->tryGetTransform()->setLocalRotation(rotation);
#endif
	}
	std::shared_ptr<Controller> cameraController() {
		auto controller = std::make_shared<Controller>();
		auto step = std::make_shared<double>(0.3);
		controller->properties.emplace("camera.speed",
			Controller::Property { ValueType::Number, [step] { return Json(*step); },
			[step](const Json& value) { *step = std::clamp(value.get<double>(), 0.01, 100.0); }, {} });
		controller->properties.emplace(
			"camera.canMove", Controller::Property { ValueType::Boolean, [] { return Json(bool(cameraObject(true))); }, {}, {} });
		controller->properties.emplace("camera.status",
			Controller::Property { ValueType::String,
			[] {
				if (cameraObject(true)) {
					return Json("Camera ready");
				}
				if (cameraObject(false)) {
					return Json("Unpack prefab to move its internal camera");
				}
				return Json("No editable main camera");
			},
			{}, {} });
		const Arguments axes { { "x", ValueType::Number }, { "y", ValueType::Number }, { "z", ValueType::Number } };
		controller->commands.emplace(
			"camera.move", Controller::Command { axes, [step](const Json& args) {
													auto camera = cameraObject(true);
													if (!camera) {
														throw std::runtime_error("No editable main camera");
													}
													auto position = camera->tryGetTransform()->getLocalPosition();
													position += vectorArgs(args) * float(*step);
													if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
														throw std::runtime_error("Camera position exceeds float range");
													}
													transform(camera, position, camera->tryGetTransform()->getLocalRotation());
												} });
		controller->commands.emplace("camera.rotate", Controller::Command { axes, [](const Json& args) {
																			   auto camera = cameraObject(true);
																			   if (!camera) {
																				   throw std::runtime_error("No editable main camera");
																			   }
																			   const MATH::QuaternionF rotation(vectorArgs(args));
																			   // Match TransformComponent::rotateLocal composition.
																			   auto old = camera->tryGetTransform()->getLocalRotation();
																			   auto next = old * rotation;
																			   transform(camera, camera->tryGetTransform()->getLocalPosition(), next);
																		   } });
		controller->commands.emplace("debug.log", Controller::Command { { { "message", ValueType::String } }, [](const Json& args) {
																		   LOG_INFO << args.at("message").get<std::string>();
																	   } });
		return controller;
	}
}
void RegisterEngineUiBindings(ViewStore& store) {
	store.factories["camera"] = cameraController;
}
}
#endif
