#pragma once
namespace IKIGAI::ECS {
class SurfaceComponent;
class ChunkModelRenderer;
class FoliageComponent;
}
namespace IKIGAI::EDITOR {
void DrawSurfaceEditor(ECS::SurfaceComponent& component);
void DrawSurfaceEditor(ECS::ChunkModelRenderer& component);
void DrawSurfaceEditor(ECS::FoliageComponent& component);
}
