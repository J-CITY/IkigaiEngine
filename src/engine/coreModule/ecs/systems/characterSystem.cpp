#include "characterSystem.h"
#include "coreModule/ecs/components/capsuleColliderComponent.h"
#include "coreModule/ecs/components/characterMotorComponent.h"
#include "coreModule/ecs/components/thirdPersonCameraComponent.h"
#include "coreModule/ecs/components/traversalComponent.h"
#include "ragdollSystem.h"
#include "traversalSystem.h"

#if USE_PHYSICS_3D
#include "coreModule/ecs/componentReference.inl"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/thirdPersonController.h"
#include "coreModule/testHooks.h"
#include "resourceModule/serviceManager.h"
#include "windowModule/window/window.h"
#include <limits>
namespace IKIGAI::ECS {
namespace {
	using namespace PHYSICS::CHARACTER;
	using PHYSICS::Vec3;
	namespace Motion = SKELETON::CHARACTER;
	constexpr float radians = .01745329252f;
	Vec3 rotate(Vec3 v, float heading) {
		return MATH::QuaternionF(Vec3 { 0, heading, 0 }) * v;
	}
	void setWorldPosition(TransformComponent& transform, Vec3 position) {
		if (auto parent = transform.obj->getParent()) {
			position = MATH::Matrix4f::Inverse(parent->tryGetTransform()->getWorldMatrix()) * position;
		}
		transform.setLocalPosition(position);
	}
	void setWorldRotation(TransformComponent& transform, MATH::QuaternionF rotation) {
		if (auto parent = transform.obj->getParent()) {
			rotation = MATH::QuaternionF::Conjugate(parent->tryGetTransform()->getWorldRotation()) * rotation;
		}
		transform.setLocalRotation(rotation);
	}

}

void CharacterSystem::fixedUpdate(SCENE_SYSTEM::Scene& scene, float dt) {
	for (auto& object : scene.getObjects()) {
		if (object->getIsActive()) {
			if (auto motor = object->getComponent<CharacterMotorComponent>()) {
				if (RagdollSystem::controlsMotor(scene, *motor)) {
					motor->wish = {};
					motor->jump = false;
					continue;
				}
				auto& m = *motor;
				if (m.settings.Capsule.empty()) {
					continue;
				}
				if (!std::isfinite(m.settings.StepHeight) || m.settings.StepHeight < 0 || !std::isfinite(m.settings.SlopeCos)
					|| m.settings.SlopeCos <= 0 || m.settings.SlopeCos > 1 || !std::isfinite(m.settings.JumpSpeed) || m.settings.JumpSpeed < 0
					|| !std::isfinite(m.settings.Gravity) || m.settings.Gravity < 0) {
					throw std::runtime_error("Invalid character motor settings");
				}
				auto collider = Resolve<CapsuleColliderComponent>(scene, m.settings.Capsule);
				if (!collider) {
					throw std::runtime_error("Character motor capsule is missing");
				}
				if (object->getComponent<Physics3DComponent>()) {
					throw std::runtime_error("Character motor and rigid body cannot both drive a transform");
				}
				if (!m.initialized) {
					m.spawn = object->tryGetTransform()->getWorldPosition();
					m.motor.reset(m.spawn);
					m.previous = m.rendered = m.spawn;
					m.initialized = true;
				}
				m.motor.capsule = collider->shape(m.motor.capsule.feet);
				m.motor.stepHeight = m.settings.StepHeight;
				m.motor.slopeCos = m.settings.SlopeCos;
				m.motor.jumpSpeed = m.settings.JumpSpeed;
				m.motor.gravity = m.settings.Gravity;
				m.fixedDt = dt;
				m.remainder = std::max(0.f, m.remainder - dt);
				m.previous = m.motor.capsule.feet;
				m.jumpAccepted = m.landed = false;
				auto traversal = object->getComponent<TraversalComponent>();
				if (traversal && traversal->settings.Actions.empty()) {
					traversal = nullptr;
				}
				TraversalComponent empty(*object);
				auto& t = traversal ? *traversal : empty;
				t.accepted = t.completed = t.cancelled = false;
				if (traversal) {
					auto target = Resolve<CharacterMotorComponent>(scene, t.settings.Motor);
					if (!target || target.get() != motor.get()) {
						throw std::runtime_error("Traversal must target its owning motor");
					}
					TraversalSystem::prepare(t);
				}
				if (TESTHOOK::characterBeforeMove && TESTHOOK::characterBeforeMove(scene, m, t, dt)) {
					continue;
				}
				auto& world = scene.physics3d();
				if (m.motor.capsule.feet.y < -10) {
					m.motor.reset(m.spawn);
					t.traversing = false;
					m.previous = m.spawn;
				}
				if (traversal && m.jump && !t.traversing && TraversalSystem::tryStart(world, m, t, unit(m.wish, { 0, 0, 0 }))) {
					m.jump = false;
				}
				if (TESTHOOK::characterAfterAttempt) {
					TESTHOOK::characterAfterAttempt(scene, m, t);
				}
				if (t.traversing) {
					TraversalSystem::update(scene, m, t, dt);
				}
				else {
					const bool wasGrounded = m.motor.grounded;
					const float impact = m.motor.velocity.y;
					m.motor.move(world, m.wish, m.jump, dt);
					m.jump = false;
					m.jumpAccepted = wasGrounded && !m.motor.grounded && m.motor.velocity.y > 0;
					m.airTime = m.motor.grounded ? m.airTime : m.airTime + dt;
					m.landed = !wasGrounded && m.motor.grounded;
					m.landingAirTime = m.airTime;
					m.impactSpeed = -impact;
					if (!wasGrounded && m.motor.grounded) {
						m.airTime = 0;
					}
					if (m.motor.steppedUp) {
						m.stepAge = 0;
					}
					else {
						m.stepAge = std::min(100.f, m.stepAge + dt);
					}
					t.poseOffset = {};
				}
				if (!t.traversing && length(Vec3 { m.motor.velocity.x, 0, m.motor.velocity.z }) > .15f) {
					const float change = Motion::angleDelta(m.heading, m.desiredHeading) * (1 - std::exp(-14 * dt));
					m.heading += change;
					m.turnRate += (change / std::max(.001f, dt) - m.turnRate) * (1 - std::exp(-10 * dt));
				}
				else {
					m.turnRate *= std::exp(-10 * dt);
				}

				setWorldPosition(*m.obj->tryGetTransform(), m.motor.capsule.feet);
			}
		}
	}
}
void CharacterSystem::update(SCENE_SYSTEM::Scene& scene, float dt) {
	for (auto& o : scene.getObjects()) {
		if (o->getIsActive()) {
			if (auto c = o->getComponent<ThirdPersonController>()) {
				c->readInput(scene);
			}
		}
	}
	for (auto& o : scene.getObjects()) {
		if (o->getIsActive()) {
			if (auto m = o->getComponent<CharacterMotorComponent>()) {
				if (RagdollSystem::controlsMotor(scene, *m)) {
					continue;
				}
				if (!m->initialized) {
					continue;
				}
				m->remainder += dt;
				const float alpha = std::clamp(m->remainder / m->fixedDt, 0.f, 1.f);
				m->rendered = m->previous * (1 - alpha) + m->motor.capsule.feet * alpha;
				if (!m->settings.Visual.empty()) {
					auto visual = Resolve<TransformComponent>(scene, m->settings.Visual);
					if (!visual) {
						throw std::runtime_error("Missing character visual transform");
					}
					setWorldPosition(*visual, m->rendered);
					setWorldRotation(*visual, MATH::QuaternionF(Vec3 { 0, m->heading, 0 }));
				}
			}
		}
	}
	for (auto& o : scene.getObjects()) {
		if (o->getIsActive()) {
			if (auto c = o->getComponent<ThirdPersonCameraComponent>()) {
				if (c->settings.Target.empty() && c->settings.Camera.empty()) {
					continue;
				}
				auto motor = Resolve<CharacterMotorComponent>(scene, c->settings.Target);
				auto camera = Resolve<CameraComponent>(scene, c->settings.Camera);
				if (!motor || !camera) {
					throw std::runtime_error("Camera target reference is missing");
				}
				auto t = motor->obj->getComponent<TraversalComponent>();
				const Vec3 pivot = (t && t->traversing			? t->visualFeet
										   : motor->initialized ? motor->rendered
																: motor->obj->tryGetTransform()->getWorldPosition())
					+ Vec3 { 0, c->settings.PivotHeight, 0 };
				const Vec3 forward { std::sin(c->yaw * radians) * std::cos(c->pitch * radians), -std::sin(c->pitch * radians),
					std::cos(c->yaw * radians) * std::cos(c->pitch * radians) };
				const float desired = c->settings.Distance;
				if (!std::isfinite(desired) || desired < .5f) {
					throw std::runtime_error("Invalid camera distance");
				}
				const auto hit = sweep(scene.physics3d(), Capsule { pivot - Vec3 { 0, .18f, 0 }, .18f, .36f }, forward * (-desired));
				const float safe = hit ? std::max(.15f, desired * hit.fraction - .06f) : desired;
				c->arm = safe < c->arm ? safe : c->arm + (safe - c->arm) * (1 - std::exp(-8 * dt));
				setWorldPosition(*camera->obj->tryGetTransform(), pivot - forward * c->arm);
				setWorldRotation(*camera->obj->tryGetTransform(), MATH::QuaternionF(Vec3 { c->pitch, c->yaw, 0 }));
			}
		}
	}
}
void CharacterSystem::registerProperties() {
	using T = DATA::ValueType;
	auto motor = [](const char* name, T type, auto get) {
		PropertyRegistry::add("CharacterMotorComponent", name,
			{ type, [get](ComponentBase& c) -> DATA::Value { return get(static_cast<CharacterMotorComponent&>(c)); } });
	};
	motor("Velocity", T::Vector3, [](auto& c) { return c.motor.velocity; });
	motor("Grounded", T::Bool, [](auto& c) { return c.motor.grounded; });
	motor("DesiredVelocity", T::Vector3, [](auto& c) { return c.wish; });
	motor("VerticalSpeed", T::Float, [](auto& c) { return c.motor.velocity.y; });
	motor("TurnRate", T::Float, [](auto& c) { return c.turnRate; });
	motor("AirTime", T::Float, [](auto& c) { return c.airTime; });
	motor("TimeSinceStep", T::Float, [](auto& c) { return c.stepAge; });
	motor("LandingAirTime", T::Float, [](auto& c) { return c.landingAirTime; });
	motor("ImpactSpeed", T::Float, [](auto& c) { return c.impactSpeed; });
	motor("SprintRequested", T::Bool, [](auto& c) { return c.sprint; });
	motor("JumpAccepted", T::Bool, [](auto& c) { return c.jumpAccepted; });
	motor("Landed", T::Bool, [](auto& c) { return c.landed; });
	motor("LocalVelocityX", T::Float,
		[](auto& c) { return c.motor.velocity.dot(Vec3 { -std::cos(c.heading * radians), 0, std::sin(c.heading * radians) }); });
	motor("LocalVelocityZ", T::Float,
		[](auto& c) { return c.motor.velocity.dot(Vec3 { std::sin(c.heading * radians), 0, std::cos(c.heading * radians) }); });
	auto traversal = [](const char* name, T type, auto get) {
		PropertyRegistry::add(
			"TraversalComponent", name, { type, [get](ComponentBase& c) -> DATA::Value { return get(static_cast<TraversalComponent&>(c)); } });
	};
	traversal("Traversing", T::Bool, [](auto& c) { return c.traversing; });
	traversal("TraversalVault", T::Bool, [](auto& c) { return c.vault; });
	traversal("TraversalProgress", T::Float, [](auto& c) { return c.progress; });
	traversal("ObstacleHeight", T::Float, [](auto& c) { return c.obstacleHeight; });
	traversal("TraversalAccepted", T::Bool, [](auto& c) { return c.accepted; });
	traversal("TraversalCompleted", T::Bool, [](auto& c) { return c.completed; });
	traversal("TraversalCancelled", T::Bool, [](auto& c) { return c.cancelled; });
	traversal("ActionId", T::Int, [](auto& c) { return c.actionId; });
	traversal("PoseOffset", T::Vector3, [](auto& c) { return c.poseOffset; });
}
}
#endif
