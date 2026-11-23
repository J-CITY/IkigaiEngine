#include "guiAnimationSystem.h"

#include "coreModule/ecs/object.h"
#include "coreModule/gui/components/guiAnimationComponent.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "resourceModule/serviceManager.h"

namespace IKIGAI::ECS {
GuiAnimationSystem::GuiAnimationSystem() {
	mName = "GuiAnimationSystem";
	setPhases({ SystemPhase::Update });
	mPriority = 14;
	// Bindings may write a transform, sprite, vector shape or label on a child.
	mExclusiveBatch = true;
	auto components = RESOURCES::ServiceManager::Get<World>().getComponentManager();
	mReads.insert(components->getComponentType<InteractionComponent>());
	mWrites.insert(components->getComponentType<GuiAnimationComponent>());
	mWrites.insert(components->getComponentType<TransformComponent>());
	mWrites.insert(components->getComponentType<VectorShapeComponent>());
	mWrites.insert(components->getComponentType<SdfTextComponent>());
	mWrites.insert(components->getComponentType<SpriteComponent>());
	mWrites.insert(components->getComponentType<SpriteAnimateComponent>());
	mWrites.insert(components->getComponentType<LabelComponent>());
}

void GuiAnimationSystem::onUpdate(World& world, CommandBuffer&, std::chrono::duration<double> dt) {
	auto& components = *world.getComponentManager();
	for (auto& animation : components.getComponentsArray<GuiAnimationComponent>()) {
		if (!animation.obj->isAlive() || !animation.obj->getIsActive()) {
			continue;
		}
		if (const auto* interaction = std::as_const(components).tryGetComponent<InteractionComponent>(animation.obj->getID())) {
			for (const auto& event : interaction->guiEvents()) {
				animation.trigger(event.type);
			}
		}
		animation.advance(static_cast<float>(dt.count()));
	}
}
}
