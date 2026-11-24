#pragma once

namespace IKIGAI::RENDER {
class MaterialInterface;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::EDITOR {
void DrawLayeredMaterialPlayground(SCENE_SYSTEM::Scene& scene);
void DrawPbrMaterialEditor(RENDER::MaterialInterface& material);
}
