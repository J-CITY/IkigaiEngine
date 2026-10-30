#include "environmentSystem.h"
#include "coreModule/ecs/components/environmentComponent.h"
#include "sceneModule/scene.h"
#if USE_PHYSICS_3D
#include "physicsModule/characterMotor.h"
#endif

namespace IKIGAI::ECS {
EnvironmentComponent* EnvironmentSystem::active(SCENE_SYSTEM::Scene& scene) {
	EnvironmentComponent* result = nullptr;
	for (const auto& object : scene.getObjects()) {
		if (!object->isAlive() || !object->getIsActive()) {
			continue;
		}
		if (auto component = object->getComponent<EnvironmentComponent>()) {
			if (result) {
				throw std::runtime_error("A scene may have only one active EnvironmentComponent");
			}
			result = component.get();
		}
	}
	return result;
}
void EnvironmentSystem::update(SCENE_SYSTEM::Scene& scene, double dt) {
	if (auto* component = active(scene)) {
		component->update(scene, dt);
		if (auto camera = scene.findMainCamera()) {
			ENVIRONMENT::WeatherRuntime::Trace trace = {};
#if USE_PHYSICS_3D
			trace = [&scene](MATH::Vector3f origin, MATH::Vector3f delta) -> std::optional<ENVIRONMENT::WeatherHit> {
				const auto hit = PHYSICS::CHARACTER::raycast(scene.physics3d(), origin, delta);
				if (hit) {
					return ENVIRONMENT::WeatherHit { hit.point, hit.normal };
				}
				return {};
			};
#endif
			component->weather().update(
				component->simulationSettings(), component->runtime().gpu(), camera->obj->getTransform()->getWorldPosition(), float(dt), trace);
			component->surfaceWeather().update(
				component->simulationSettings(), component->runtime().gpu(), camera->obj->getTransform()->getWorldPosition(), float(dt), trace);
		}
	}
}
}
