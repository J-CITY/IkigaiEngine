#pragma once

#include "coreModule/gui/components/spriteComponent.h"
#include "ecsModule/systemManager.h"
#include "windowModule/inputManager/inputManager.h"

namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}

namespace IKIGAI::ECS {

class GuiInputSystem : public System {
public:
	GuiInputSystem();
	~GuiInputSystem() override;
	void onPhase(SystemPhase phase, World& world, CommandBuffer& cb, std::chrono::duration<double> dt, const SystemPhaseContext* context) override;
	void onUpdate(World& world, CommandBuffer& cb, std::chrono::duration<double> dt) override;

private:
	using Target = UTILS::WeakPtr<InteractionComponent>;
	void updateScene(SCENE_SYSTEM::Scene& scene, std::chrono::duration<double> dt);
	void setHover(Target target, MATH::Vector2f position, std::int64_t pointerId);
	void setFocus(Target target, MATH::Vector2f position, std::int64_t pointerId);
	void finishPress(bool cancelled, bool inside);
	void releaseCapture();

	Target mHovered;
	Target mFocused;
	Target mPressed;
	std::optional<std::int64_t> mPointer;
	std::optional<INPUT_SYSTEM::EKey> mKey;
	std::optional<int> mGamepad;
	MATH::Vector2f mPressPosition {};
	bool mHoldActivated = false;
	bool mTouchHover = false;
};

}
