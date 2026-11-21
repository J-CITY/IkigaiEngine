#pragma once
#include "ecsModule/world.h"
#include "sceneManager.h"

namespace IKIGAI::SCENE_SYSTEM {
struct SceneManager::LoadContext {
	std::unique_ptr<ECS::World> world;
	std::unique_ptr<Scene> scene;
	std::vector<SceneObjectDescriptor> descriptors;
	std::vector<std::shared_ptr<void>> resources;
};
struct SceneManager::LoadingSceneCache {
	std::unique_ptr<ECS::World> world;
	std::unique_ptr<Scene> scene;
	std::vector<std::shared_ptr<void>> resources;
};
}
