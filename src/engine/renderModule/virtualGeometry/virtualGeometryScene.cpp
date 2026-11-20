#include "virtualGeometryScene.h"

#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/virtualMeshRenderer.h"
#include "coreModule/ecs/object.h"
#include "ecsModule/world.h"
#include "sceneModule/scene.h"

#include <cstddef>

namespace IKIGAI::RENDER {
std::vector<VG::VirtualGeometryInstance> CollectVirtualGeometryInstances(SCENE_SYSTEM::Scene& scene, uint64_t frame, bool collectShadows) {
	auto& components = scene.getWorld().getComponentManager()->getComponentsArray<ECS::VirtualMeshRenderer>();
	std::vector<VG::VirtualGeometryInstance> instances;
	instances.reserve(static_cast<std::size_t>(components.getSize()));
	for (auto& component : components) {
		auto& object = component.obj.get();
		if (!object.getIsVisible() || !object.getIsActive()) {
			continue;
		}
		auto& transform = object.getTransform()->getTransform();
		auto& instance = instances.emplace_back();
		instance.Asset = component.getAsset();
		instance.Fallback = component.getFallbackModel();
		instance.Materials = component.getMaterials();
		instance.World = transform.getWorldMatrix();
		instance.PreviousWorld = component.advanceHistory(instance.World, frame);
		instance.Position = transform.getWorldPosition();
		instance.InstanceId = static_cast<uint64_t>(object.getID().getUniqueId());
		instance.ErrorThreshold = component.getErrorThreshold();
		instance.DebugMode = component.getDebugMode();
		instance.ContinuousHistory = component.hasContinuousHistory();
		instance.AllowClusterShadows = collectShadows && !object.getComponent<ECS::ModelRenderer>();
	}
	return instances;
}
}
