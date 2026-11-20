#ifdef USE_EDITOR
#include "componentEditorHooks.h"
#include "coreModule/ecs/components/audioComponent.h"
#include "coreModule/ecs/components/behaviorTreeComponent.h"
#include "coreModule/ecs/components/blackboardComponent.h"
#include "coreModule/ecs/components/inputComponent.h"
#include "coreModule/ecs/components/logicComponent.h"
#include "coreModule/ecs/components/propertyAnimationComponent.h"
#include "coreModule/ecs/components/variableContextComponent.h"
#if USE_IKIGAI_SCRIPT
#include "coreModule/ecs/components/ikigaiScriptComponent.h"
#endif
#if USE_LUA
#include "coreModule/ecs/components/scriptComponent.h"
#endif

namespace IKIGAI::EDITOR::detail {
void registerGameplayComponentEditors(ComponentEditorRegistry& registry) {
	addComponentEditorHooks<ECS::AudioComponent, ECS::AudioListenerComponent, ECS::BehaviorTreeComponent, ECS::BlackboardComponent,
		ECS::InputComponent, ECS::LogicComponent, ECS::PropertyAnimationComponent, ECS::VariableContextComponent>(registry);
#if USE_IKIGAI_SCRIPT
	addComponentEditorHooks<ECS::IkigaiScriptComponent>(registry);
#endif
#if USE_LUA
	addComponentEditorHooks<ECS::ScriptComponent>(registry);
#endif
}
}
#endif
