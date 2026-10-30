#pragma once
namespace IKIGAI::ECS {
class EnvironmentComponent;
class VegetationInteractorComponent;
}
namespace IKIGAI::EDITOR {
void DrawEnvironmentEditor(ECS::EnvironmentComponent& component);
void DrawVegetationInteractorEditor(ECS::VegetationInteractorComponent& component);
}
