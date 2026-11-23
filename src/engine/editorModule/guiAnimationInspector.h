#pragma once

#ifdef USE_EDITOR
namespace IKIGAI::ECS {
class GuiAnimationComponent;
}

namespace IKIGAI::EDITOR {
void drawGuiAnimationInspector(ECS::GuiAnimationComponent& component);
}
#endif
