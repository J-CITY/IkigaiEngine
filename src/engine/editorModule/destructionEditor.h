#pragma once
#if defined(USE_EDITOR) && USE_PHYSICS_3D
namespace IKIGAI::ECS {
class DestructibleMeshComponent;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::EDITOR {
void DrawDestructionPlayground(SCENE_SYSTEM::Scene& scene, bool acceptSceneClick);
void DrawDestructionEditor(ECS::DestructibleMeshComponent& component);
}
#endif
