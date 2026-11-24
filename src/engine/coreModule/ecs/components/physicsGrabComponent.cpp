#include "physicsGrabComponent.h"
#if USE_PHYSICS_3D
#include "cameraComponent.h"
#include "coreModule/ecs/componentCatalog.h"
#include "physicsModule/characterMotor.h"
#include "sceneModule/sceneManager.h"
#include "windowModule/inputManager/inputManager.h"
#include "windowModule/window/window.h"

namespace IKIGAI::ECS {
namespace {
	[[maybe_unused]] const bool registered = ComponentCatalog::enroll<PhysicsGrabComponent>();
}
PhysicsGrabComponent::PhysicsGrabComponent(UTILS::Ref<Object> owner)
	: ComponentBase(owner) {
	__NAME__ = GetComponentName<PhysicsGrabComponent>();
}
PhysicsGrabComponent::PhysicsGrabComponent(UTILS::Ref<Object> owner, const ComponentBase::Descriptor&)
	: PhysicsGrabComponent(owner) { }
PhysicsGrabComponent::Descriptor PhysicsGrabComponent::getDescriptor() const {
	return { {}, GetType<PhysicsGrabComponent>() };
}
void PhysicsGrabComponent::onDisable() {
	mBody = 0;
	mWorld.reset();
}
void PhysicsGrabComponent::onUpdate(std::chrono::duration<double>) {
#if defined(USE_SDL)
	using namespace INPUT_SYSTEM;
	auto& input = RESOURCES::ServiceManager::Get<InputManager>();
	auto& window = RESOURCES::ServiceManager::Get<WINDOW::Window>();
	const auto button = EMouseButton::MOUSE_BUTTON_LEFT;
	if (!window.hasFocus() || input.getUiCapture().mouse || input.isPointerCaptured(kMousePointerId) || !input.isMouseButtonPressed(button)) {
		onDisable();
		return;
	}
	const bool pressed = input.wasMouseButtonPressedThisFrame(button);
	if (!pressed && !mBody) {
		return;
	}
	auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	const auto camera = scene.findMainCamera();
	const auto size = window.getSize();
	if (!camera || &camera->obj.get() != &obj.get() || !size.x || !size.y) {
		onDisable();
		return;
	}
	const auto& view = camera->getCamera().getViewMatrix();
	const auto inverse = MATH::Matrix4f::Inverse(camera->getCamera().getProjectionMatrix() * view);
	const auto cursorRay = [&](MATH::Vector2f mouse) {
		const float x = 2.f * mouse.x / size.x - 1.f;
		const float y = 1.f - 2.f * mouse.y / size.y;
		const auto unproject = [&](float z) {
			const auto p = inverse * MATH::Vector4f { x, y, z, 1 };
			return PHYSICS::Vec3 { p.x, p.y, p.z } * (1.f / p.w);
		};
		const auto start = unproject(-1);
		return std::pair { start, unproject(1) - start };
	};
	const auto mouse = input.getMousePosition();
	const auto [start, delta] = cursorRay({ float(mouse.x), float(mouse.y) });
	if (!PHYSICS_COMMON::finite(start) || !PHYSICS_COMMON::finite(delta)) {
		onDisable();
		return;
	}
	if (pressed) {
		onDisable();
		// A frame can receive the press and several moves. Pick at the press, then follow the current cursor.
		const auto events = input.pointerEventsThisFrame();
		const auto press = std::find_if(events.rbegin(), events.rend(),
			[](const auto& event) { return event.pointerId == kMousePointerId && event.phase == PointerPhase::Began; });
		if (press == events.rend()) {
			return;
		}
		const auto [pressStart, pressDelta] = cursorRay(press->position);
		auto world = scene.physics3dShared();
		const auto hit = PHYSICS::CHARACTER::raycast(*world, pressStart, pressDelta, { .layer = ~0u, .staticsOnly = false });
		auto* body = hit ? world->body(hit.body) : nullptr;
		if (!body || body->type != PHYSICS::BodyType::Dynamic) {
			return;
		}
		mWorld = world;
		mBody = hit.body;
		mLocalAnchor = body->getPointInLocalSpace(hit.point);
		mPlanePoint = hit.point;
		mTarget = hit.point;
		mPlaneNormal = obj->getTransform()->getWorldForward();
	}
	const float denominator = delta.dot(mPlaneNormal);
	if (std::abs(denominator) > 1e-6f) {
		mTarget = start + delta * ((mPlanePoint - start).dot(mPlaneNormal) / denominator);
	}
#endif
}
void PhysicsGrabComponent::onFixedUpdate(std::chrono::duration<double> dt) {
	if (!mBody) {
		return;
	}
	auto world = mWorld.lock();
	auto* body = world ? world->body(mBody) : nullptr;
	if (!body) {
		onDisable();
		return;
	}
	auto& scene = RESOURCES::ServiceManager::Get<SCENE_SYSTEM::SceneManager>().getCurrentScene();
	if (scene.physics3dPaused()) {
		return;
	}
	const auto anchor = body->getPointInWorldSpace(mLocalAnchor);
	const auto arm = anchor - body->getPosition();
	const auto velocity = body->getVelocity() + body->getRotation().cross(arm);
	// A damped point servo. Effective mass accounts for the leverage at the picked point.
	const auto acceleration = (mTarget - anchor) * 180.f - velocity * 24.f;
	const float magnitude = acceleration.magnitude();
	if (magnitude < 1e-5f) {
		return;
	}
	const auto direction = acceleration * (1.f / magnitude);
	const auto lever = arm.cross(direction);
	const float inverseMass = body->getInverseMass() + lever.dot(body->getInverseInertiaTensorWorld().transform(lever));
	world->applyImpulse(mBody, direction * (std::min(magnitude, 120.f) * static_cast<float>(dt.count()) / inverseMass), anchor);
}
}
#endif
