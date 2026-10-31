#pragma once
#if defined(USE_EDITOR) && USE_PHYSICS_3D
namespace IKIGAI::ECS {
class DestructibleMeshComponent;
}
namespace IKIGAI::EDITOR {
void DrawDestructionEditor(ECS::DestructibleMeshComponent& component);
}
#endif
