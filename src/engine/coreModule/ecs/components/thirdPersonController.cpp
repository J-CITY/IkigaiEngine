#include "thirdPersonController.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/componentReference.inl"
#include "coreModule/ecs/components/capsuleColliderComponent.h"
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/thirdPersonCameraComponent.h"
#include "coreModule/ecs/components/traversalComponent.h"
#include "coreModule/ecs/systems/characterSystem.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/inputManager/inputManager.h"
#include "windowModule/window/window.h"
#ifdef USE_EDITOR
#include <imgui.h>
#endif
namespace IKIGAI::ECS {
ThirdPersonController::ThirdPersonController(UTILS::Ref<Object> o)
	: ComponentBase(o) {
	__NAME__ = "ThirdPersonController";
	settings.Type = GetType<ThirdPersonController>();
}
ThirdPersonController::ThirdPersonController(UTILS::Ref<Object> o, const ComponentBase::Descriptor& d)
	: ThirdPersonController(o) {
	settings = static_cast<const Descriptor&>(d);
}
void ThirdPersonController::readInput(SCENE_SYSTEM::Scene& scene) {
	if (settings.Motor.empty() && settings.Camera.empty()) {
		return;
	}
	using namespace PHYSICS::CHARACTER;
	using PHYSICS::Vec3;
	constexpr float radians = .01745329252f;
	auto motor = Resolve<CharacterMotorComponent>(scene, settings.Motor);
	auto camera = Resolve<ThirdPersonCameraComponent>(scene, settings.Camera);
	if (!motor || !camera) {
		throw std::runtime_error("Missing controller motor/camera reference");
	}
	auto& input = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>();
	auto& window = RESOURCES::ServiceManager::Get<WINDOW::Window>();
	using K = INPUT_SYSTEM::EKey;
	bool justCaptured = false;
	if (!window.hasFocus() || input.wasKeyPressedThisFrame(K::KEY_ESCAPE)) {
		camera->onDisable();
	}
	else if (input.wasMouseButtonPressedThisFrame(INPUT_SYSTEM::EMouseButton::MOUSE_BUTTON_LEFT)) {
#ifdef USE_EDITOR
		if (!ImGui::GetIO().WantCaptureMouse)
#endif
		{
			camera->captured = true;
			justCaptured = true;
			window.setCursorVisible(false, true);
		}
	}
	if (camera->captured) {
		const auto mouse = justCaptured ? MATH::Vector2f { 0, 0 } : input.getMouseDelta();
		camera->yaw -= mouse.x * camera->settings.Sensitivity;
		camera->pitch = std::clamp(camera->pitch + mouse.y * camera->settings.Sensitivity, -35.f, 70.f);
		const float x = float(input.isKeyPressed(K::KEY_D)) - float(input.isKeyPressed(K::KEY_A));
		const float z = float(input.isKeyPressed(K::KEY_W)) - float(input.isKeyPressed(K::KEY_S));
		Vec3 local { x, 0, z };
		if (length(local) > 1) {
			local = unit(local);
		}
		const Vec3 facing = { std::sin(camera->yaw * radians), 0, std::cos(camera->yaw * radians) };
		const Vec3 right { -facing.z, 0, facing.x };
		motor->sprint = input.isKeyPressed(K::KEY_LEFT_SHIFT);
		motor->wish = (right * local.x + facing * local.z) * (motor->sprint ? settings.SprintSpeed : settings.RunSpeed);
		motor->jump |= input.wasKeyPressedThisFrame(K::KEY_SPACE);
		if (input.wasKeyPressedThisFrame(K::KEY_R)) {
			motor->motor.reset(motor->spawn);
			motor->previous = motor->rendered = motor->spawn;
		}
	}
	else {
		motor->wish = {};
	}
	motor->desiredHeading = camera->yaw;
}
}

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ThirdPersonController>();
	return true;
}();
}

#endif
