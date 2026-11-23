#pragma once

#include "ecsModule/systemManager.h"

namespace IKIGAI::ECS {
class CheckboxComponent;
class SliderComponent;
class ProgressBarComponent;
class LoadingSpinnerComponent;
class ScrollViewComponent;
class Object;

class GuiWidgetSystem : public System {
public:
	GuiWidgetSystem();
	void onUpdate(World& world, CommandBuffer& commands, std::chrono::duration<double> elapsed) override;

private:
	void updateCheckbox(CheckboxComponent& checkbox, Object& object, CommandBuffer& commands);
	void updateSlider(SliderComponent& slider, Object& object, CommandBuffer& commands);
	void updateProgressBar(ProgressBarComponent& progress, Object& object, CommandBuffer& commands);
	void updateSpinner(LoadingSpinnerComponent& spinner, Object& object, double elapsed);
	void updateScrollView(ScrollViewComponent& scrollView, Object& object, CommandBuffer& commands);
};
}
