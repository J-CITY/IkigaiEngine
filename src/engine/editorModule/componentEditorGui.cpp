#ifdef USE_EDITOR
#include "componentEditorHooks.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"

namespace IKIGAI::EDITOR::detail {
void registerGuiComponentEditors(ComponentEditorRegistry& registry) {
	addComponentEditorHooks<ECS::RootGuiComponent, ECS::SpriteComponent, ECS::SpriteAnimateComponent, ECS::SpriteParticleComponent,
		ECS::LabelComponent, ECS::SpineComponent, ECS::InteractionComponent, ECS::ClipComponent, ECS::ScrollComponent, ECS::LayoutComponent,
		ECS::SdfTextComponent, ECS::VectorShapeComponent, ECS::VectorPathComponent, ECS::VectorCanvasComponent>(registry);
}
}
#endif
