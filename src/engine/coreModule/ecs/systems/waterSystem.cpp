#include "waterSystem.h"
#include "coreModule/ecs/components/cameraComponent.h"
#include "coreModule/ecs/components/physics3dComponent.h"
#include "coreModule/ecs/components/waterComponent.h"
#include "sceneModule/scene.h"
#include <algorithm>
#include <limits>
namespace IKIGAI::ECS::WaterSystem {
void advance(SCENE_SYSTEM::Scene& scene, float dt) {
	for (auto& water : scene.getWorld().getComponentManager()->getComponentsArray<WaterComponent>()) {
		if (water.obj->getIsActive() && scene.getObjectIndex(water.obj.get())) {
			water.advance(dt);
		}
	}
}
#if USE_PHYSICS_3D
namespace {
	WaterComponent::FoamHull nearestFoamHull(SCENE_SYSTEM::Scene& scene, WaterComponent& water) {
		WaterComponent::FoamHull result;
		if (water.getDescriptor().HullFoam <= 0) {
			return result;
		}
		const auto origin = water.obj->tryGetTransform()->getWorldPosition();
		const auto extent = water.obj->tryGetTransform()->getWorldScale();
		auto camera = scene.findMainCamera();
		const auto eye = camera ? camera->obj->tryGetTransform()->getWorldPosition() : origin;
		float nearest = std::numeric_limits<float>::max();
		for (auto& hull : scene.getWorld().getComponentManager()->getComponentsArray<Physics3DComponent>()) {
			const auto* body = hull.body();
			if (!hull.settings.FloatOnWater || !body || !hull.obj->getIsActive() || !hull.obj->getIsVisible()
				|| !scene.getObjectIndex(hull.obj.get())) {
				continue;
			}
			const auto p = body->getPosition();
			const auto size = hull.settings.ShapeKind ? hull.settings.HalfSize : MATH::Vector3f(hull.settings.Radius);
			if (std::abs(p.x - origin.x) > extent.x || std::abs(p.z - origin.z) > extent.z) {
				continue;
			}
			// Check the full footprint: a pitching bow can touch a crest while the centre is dry.
			float lowest = std::numeric_limits<float>::max();
			float highest = -lowest;
			for (float x : { -0.75f, 0.75f }) {
				for (float z : { -0.75f, 0.75f }) {
					const auto probe = body->getPointInWorldSpace({ x * size.x, 0, z * size.z });
					float separation = probe.y - origin.y - water.displacement(probe.x, probe.z);
					lowest = std::min(lowest, separation);
					highest = std::max(highest, separation);
				}
			}
			if (lowest > size.y || highest < -size.y) {
				continue;
			}
			const float distance = (p - eye).squareMagnitude();
			if (distance >= nearest) {
				continue;
			}
			const auto forward = body->getDirectionInWorldSpace({ 0, 0, 1 });
			const float length = std::hypot(forward.x, forward.z);
			if (length < .001f) {
				continue;
			}
			nearest = distance;
			const auto relative = body->getVelocity() - water.velocity(p.x, p.z);
			result.footprint = { p.x, p.z, size.x, size.z };
			result.motion = { forward.x / length, forward.z / length, relative.x, relative.z };
			result.agitation = std::clamp(.12f + std::abs(relative.y) * .8f + std::hypot(relative.x, relative.z) * .9f, 0.f, 1.f);
		}
		return result;
	}
}
#endif
void present(SCENE_SYSTEM::Scene& scene) {
	for (auto& water : scene.getWorld().getComponentManager()->getComponentsArray<WaterComponent>()) {
		if (water.obj->getIsActive() && scene.getObjectIndex(water.obj.get())) {
#if USE_PHYSICS_3D
			water.present(nearestFoamHull(scene, water));
#else
			water.present({});
#endif
		}
	}
}
#if USE_PHYSICS_3D
void stepPhysics(SCENE_SYSTEM::Scene& scene, PHYSICS::PhysicWorld& world, float dt) {
	// A rejected physics tick preserves submitted forces. Restore each hull's pre-water
	// accumulators so the next attempt can resample the surface without doubling lift.
	struct Accumulators {
		MATH::Vector3f force;
		MATH::Vector3f torque;
	};
	std::map<PHYSICS::Id, Accumulators> beforeWater;
	auto cm = scene.getWorld().getComponentManager();
	for (auto& water : cm->getComponentsArray<WaterComponent>()) {
		if (!water.obj->getIsActive() || !scene.getObjectIndex(water.obj.get())) {
			continue;
		}
		const auto center = water.obj->tryGetTransform()->getWorldPosition();
		const auto extent = water.obj->tryGetTransform()->getWorldScale();
		for (auto& hull : cm->getComponentsArray<Physics3DComponent>()) {
			auto* body = hull.body();
			if (!hull.settings.FloatOnWater || !body || body->type != PHYSICS::BodyType::Dynamic || !scene.getObjectIndex(hull.obj.get())) {
				continue;
			}
			const auto size = hull.settings.ShapeKind ? hull.settings.HalfSize : MATH::Vector3f(hull.settings.Radius);
			for (float side : { -.78f, .78f }) {
				for (float end : { -.72f, .72f }) {
					const auto probe = body->getPointInWorldSpace({ side * size.x, -.65f * size.y, end * size.z });
					if (std::abs(probe.x - center.x) > extent.x || std::abs(probe.z - center.z) > extent.z) {
						continue;
					}
					float depth = center.y + water.displacement(probe.x, probe.z) - probe.y;
					if (depth <= 0) {
						continue;
					}
					float immersion = std::clamp(depth / size.y, 0.f, 2.f);
					auto relative = water.velocity(probe.x, probe.z) - body->getVelocity() - body->getRotation().cross(probe - body->getPosition());
					// Each quadrant represents a quarter of the hull's effective waterplane.
					float area = .48f * size.x * size.z;
					float mass = body->getMass() * .25f;
					auto force = relative * (mass * immersion);
					force.y
						= 1000.f * std::abs(world.settings.gravity.y) * area * std::min(depth, 2.f * size.y) + relative.y * mass * 4.f * immersion;
					beforeWater.try_emplace(body->id, Accumulators { body->force, body->torque });
					world.applyForce(body->id, force, probe);
				}
			}
		}
	}
	try {
		world.step(dt);
	} catch (const PHYSICS::ContactBudgetExceeded&) {
		for (const auto& [id, state] : beforeWater) {
			auto* body = world.body(id);
			body->force = state.force;
			body->torque = state.torque;
		}
		throw;
	}
	advance(scene, dt);
}
#endif
}
